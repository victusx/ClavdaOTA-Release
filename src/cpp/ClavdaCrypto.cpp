#include "ClavdaCrypto.h"

#include <string.h>
#include <memory>
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "mbedtls/base64.h"
#include "mbedtls/platform.h"
#ifndef CLAVDA_PSA_CRYPTO
  #include "mbedtls/gcm.h"
#endif
#if ESP_IDF_VERSION_MAJOR >= 5
  #include "esp_random.h"
#else
  #include "esp_system.h"
#endif

namespace clavda {

static const size_t IV_LEN = 12;
static const size_t TAG_LEN = 16;
static const size_t KEY_LEN = 32;
// A job is a few hundred bytes plus a presigned URL. Anything far past that
// is not one, and is not worth a heap allocation to find out.
static const size_t MAX_SEALED_B64 = 8192;

// Only where the core both pins mbedTLS to internal RAM and lets the allocator
// be swapped at runtime; a custom sdkconfig that chose otherwise is left alone.
#if defined(CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC) && defined(MBEDTLS_PLATFORM_MEMORY) && \
    !defined(MBEDTLS_PLATFORM_CALLOC_MACRO)
  #define CLAVDA_TLS_PSRAM 1

// PSRAM first, then internal RAM once PSRAM is full, rather than failing a handshake.
static IRAM_ATTR void *tlsCalloc(size_t n, size_t size) {
  return heap_caps_calloc_prefer(n, size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
#endif

bool isValidId(const char *id, size_t max) {
  if (!id) return false;
  size_t len = 0;
  for (; id[len]; len++) {
    const char c = id[len];
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                    c == '_' || c == '-';
    if (!ok || len >= max) return false;
  }
  return len > 0;
}

bool base64Decode(const char *in, uint8_t *out, size_t outCap, size_t &outLen) {
  outLen = 0;
  if (!in || !out) return false;
  return mbedtls_base64_decode(out, outCap, &outLen, (const unsigned char *)in, strlen(in)) == 0;
}

bool base64Encode(const uint8_t *data, size_t len, char *out, size_t outCap, size_t &outLen) {
  outLen = 0;
  return mbedtls_base64_encode((unsigned char *)out, outCap, &outLen, data, len) == 0;
}

bool isJobKey(const char *keyB64) {
  uint8_t key[KEY_LEN + 4];
  size_t len = 0;
  const bool ok = base64Decode(keyB64, key, sizeof(key), len) && len == KEY_LEN;
  memset(key, 0, sizeof(key));
  return ok;
}

/**
 * AES-256-GCM with a 16-byte tag. Sealing turns `in` into ciphertext | tag in
 * `out`; opening takes ciphertext | tag and writes the plaintext, only once
 * the tag verifies. `aad` may be NULL.
 */
static bool aesGcm(bool seal, const uint8_t key[32], const uint8_t iv[12], const char *aad,
                   const uint8_t *in, size_t inLen, uint8_t *out, size_t outCap) {
  const size_t aadLen = aad ? strlen(aad) : 0;
  if (!seal && inLen < TAG_LEN) return false;
  if (outCap < (seal ? inLen + TAG_LEN : inLen - TAG_LEN)) return false;
#ifdef CLAVDA_PSA_CRYPTO
  if (psa_crypto_init() != PSA_SUCCESS) return false;
  psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
  psa_set_key_bits(&attributes, KEY_LEN * 8);
  psa_set_key_usage_flags(&attributes, seal ? PSA_KEY_USAGE_ENCRYPT : PSA_KEY_USAGE_DECRYPT);
  psa_set_key_algorithm(&attributes, PSA_ALG_GCM);
  mbedtls_svc_key_id_t id = MBEDTLS_SVC_KEY_ID_INIT;
  psa_status_t status = psa_import_key(&attributes, key, KEY_LEN, &id);
  psa_reset_key_attributes(&attributes);
  if (status != PSA_SUCCESS) return false;
  size_t outLen = 0;
  if (seal) {
    status = psa_aead_encrypt(id, PSA_ALG_GCM, iv, IV_LEN, (const uint8_t *)aad, aadLen, in, inLen,
                              out, outCap, &outLen);
  } else {
    status = psa_aead_decrypt(id, PSA_ALG_GCM, iv, IV_LEN, (const uint8_t *)aad, aadLen, in, inLen,
                              out, outCap, &outLen);
  }
  psa_destroy_key(id);
  return status == PSA_SUCCESS;
#else
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, KEY_LEN * 8);
  if (rc == 0 && seal) {
    rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, inLen, iv, IV_LEN,
                                   (const unsigned char *)aad, aadLen, in, out, TAG_LEN,
                                   out + inLen);
  } else if (rc == 0) {
    const size_t ctLen = inLen - TAG_LEN;
    rc = mbedtls_gcm_auth_decrypt(&gcm, ctLen, iv, IV_LEN, (const unsigned char *)aad, aadLen,
                                  in + ctLen, TAG_LEN, in, out);
  }
  mbedtls_gcm_free(&gcm);
  return rc == 0;
#endif
}

bool openJob(const char *jobKeyB64, const char *sealedB64, std::string &plain,
             std::string &error) {
  uint8_t key[KEY_LEN + 4];
  size_t keyLen = 0;
  if (!base64Decode(jobKeyB64, key, sizeof(key), keyLen) || keyLen != KEY_LEN) {
    memset(key, 0, sizeof(key));
    error = "This device has no valid job key";
    return false;
  }

  const size_t sealedLen = sealedB64 ? strlen(sealedB64) : 0;
  if (sealedLen == 0 || sealedLen > MAX_SEALED_B64) {
    memset(key, 0, sizeof(key));
    error = "Malformed job";
    return false;
  }

  const size_t cap = (sealedLen / 4) * 3 + 3;
  std::unique_ptr<uint8_t[]> raw(new (std::nothrow) uint8_t[cap]);
  std::unique_ptr<uint8_t[]> out(new (std::nothrow) uint8_t[cap + 1]);
  size_t rawLen = 0;
  if (!raw || !out) {
    memset(key, 0, sizeof(key));
    error = "Out of memory opening the job";
    return false;
  }
  if (!base64Decode(sealedB64, raw.get(), cap, rawLen) || rawLen <= IV_LEN + TAG_LEN) {
    memset(key, 0, sizeof(key));
    error = "Malformed job";
    return false;
  }

  const size_t ctLen = rawLen - IV_LEN - TAG_LEN;
  const bool ok =
    aesGcm(false, key, raw.get(), nullptr, raw.get() + IV_LEN, rawLen - IV_LEN, out.get(), cap);
  memset(key, 0, sizeof(key));
  if (!ok) {
    // Wrong key and tampered ciphertext look the same from here.
    error = "The job did not verify: it was not sealed for this device";
    return false;
  }

  plain.assign((const char *)out.get(), ctLen);
  return true;
}

bool sealBytes(const uint8_t key[32], const uint8_t *plain, size_t len, const char *aad,
               uint8_t *out) {
  // A fresh random nonce per frame; a session key never sees anywhere near
  // the 2^32 frames where that would start to matter.
  esp_fill_random(out, IV_LEN);
  return aesGcm(true, key, out, aad, plain, len, out + IV_LEN, len + TAG_LEN);
}

bool openBytes(const uint8_t key[32], const char *sealedB64, const char *aad, uint8_t *out,
               size_t outCap, size_t &outLen) {
  outLen = 0;
  const size_t sealedLen = sealedB64 ? strlen(sealedB64) : 0;
  if (sealedLen == 0 || sealedLen > MAX_SEALED_B64) return false;

  const size_t cap = (sealedLen / 4) * 3 + 3;
  std::unique_ptr<uint8_t[]> raw(new (std::nothrow) uint8_t[cap]);
  size_t rawLen = 0;
  if (!raw || !base64Decode(sealedB64, raw.get(), cap, rawLen) || rawLen < IV_LEN + TAG_LEN) {
    return false;
  }
  const size_t ctLen = rawLen - IV_LEN - TAG_LEN;
  if (ctLen > outCap) return false;
  if (!aesGcm(false, key, raw.get(), aad, raw.get() + IV_LEN, rawLen - IV_LEN, out, outCap)) {
    return false;
  }
  outLen = ctLen;
  return true;
}

bool tlsPreferPsram() {
#ifdef CLAVDA_TLS_PSRAM
  if (!heap_caps_get_total_size(MALLOC_CAP_SPIRAM)) return false;
  // heap_caps_free() frees from any region, so blocks the core's allocator
  // handed out before the switch - Wi-Fi's, say - are still freed correctly.
  mbedtls_platform_set_calloc_free(tlsCalloc, heap_caps_free);
  return true;
#else
  return false;
#endif
}

#ifdef CLAVDA_PSA_CRYPTO

Sha256::Sha256() : _op(PSA_HASH_OPERATION_INIT) {}

Sha256::~Sha256() {
  psa_hash_abort(&_op);
}

void Sha256::begin() {
  psa_crypto_init();
  psa_hash_abort(&_op);
  _op = PSA_HASH_OPERATION_INIT;
  psa_hash_setup(&_op, PSA_ALG_SHA_256);
}

void Sha256::update(const uint8_t *data, size_t len) {
  psa_hash_update(&_op, data, len);
}

#else

Sha256::Sha256() {
  mbedtls_sha256_init(&_ctx);
}

Sha256::~Sha256() {
  mbedtls_sha256_free(&_ctx);
}

// mbedTLS 3 (IDF 5, Arduino-ESP32 3.x) dropped the _ret suffixes that 2.x
// (IDF 4.4, Arduino-ESP32 2.x) needs to avoid the deprecated void variants.
void Sha256::begin() {
  mbedtls_sha256_free(&_ctx);
  mbedtls_sha256_init(&_ctx);
  #if MBEDTLS_VERSION_NUMBER >= 0x03000000
  mbedtls_sha256_starts(&_ctx, 0);
  #else
  mbedtls_sha256_starts_ret(&_ctx, 0);
  #endif
}

void Sha256::update(const uint8_t *data, size_t len) {
  #if MBEDTLS_VERSION_NUMBER >= 0x03000000
  mbedtls_sha256_update(&_ctx, data, len);
  #else
  mbedtls_sha256_update_ret(&_ctx, data, len);
  #endif
}

#endif

void Sha256::finishHex(char out[65]) {
  uint8_t digest[32] = {0};
#ifdef CLAVDA_PSA_CRYPTO
  size_t digestLen = 0;
  if (psa_hash_finish(&_op, digest, sizeof(digest), &digestLen) != PSA_SUCCESS) {
    // An empty digest matches no job's SHA-256, so the image is refused.
    out[0] = 0;
    return;
  }
#elif MBEDTLS_VERSION_NUMBER >= 0x03000000
  mbedtls_sha256_finish(&_ctx, digest);
#else
  mbedtls_sha256_finish_ret(&_ctx, digest);
#endif
  static const char HEX_DIGITS[] = "0123456789abcdef";
  for (int i = 0; i < 32; i++) {
    out[i * 2] = HEX_DIGITS[digest[i] >> 4];
    out[i * 2 + 1] = HEX_DIGITS[digest[i] & 0x0f];
  }
  out[64] = 0;
}

}  // namespace clavda
