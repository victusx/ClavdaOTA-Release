#include "ClavdaCrypto.h"

#include <memory>
#include "esp_idf_version.h"
#include "mbedtls/base64.h"
#include "mbedtls/gcm.h"
#include "mbedtls/version.h"
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

bool openJob(const char *jobKeyB64, const char *sealedB64, String &plain, String &error) {
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
  const uint8_t *iv = raw.get();
  const uint8_t *ct = raw.get() + IV_LEN;
  const uint8_t *tag = raw.get() + IV_LEN + ctLen;

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, KEY_LEN * 8);
  if (rc == 0) {
    rc = mbedtls_gcm_auth_decrypt(&gcm, ctLen, iv, IV_LEN, nullptr, 0, tag, TAG_LEN, ct, out.get());
  }
  mbedtls_gcm_free(&gcm);
  memset(key, 0, sizeof(key));

  if (rc != 0) {
    // Wrong key and tampered ciphertext look the same from here.
    error = "The job did not verify: it was not sealed for this device";
    return false;
  }

  out[ctLen] = 0;
  plain = String((const char *)out.get());
  return true;
}

bool sealBytes(const uint8_t key[32], const uint8_t *plain, size_t len, const char *aad,
               uint8_t *out) {
  uint8_t *iv = out;
  uint8_t *ct = out + IV_LEN;
  uint8_t *tag = out + IV_LEN + len;
  // A fresh random nonce per frame; a session key never sees anywhere near
  // the 2^32 frames where that would start to matter.
  esp_fill_random(iv, IV_LEN);

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, KEY_LEN * 8);
  if (rc == 0) {
    rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, len, iv, IV_LEN,
                                   (const unsigned char *)aad, strlen(aad), plain, ct, TAG_LEN, tag);
  }
  mbedtls_gcm_free(&gcm);
  return rc == 0;
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

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, KEY_LEN * 8);
  if (rc == 0) {
    rc = mbedtls_gcm_auth_decrypt(&gcm, ctLen, raw.get(), IV_LEN, (const unsigned char *)aad,
                                  strlen(aad), raw.get() + IV_LEN + ctLen, TAG_LEN,
                                  raw.get() + IV_LEN, out);
  }
  mbedtls_gcm_free(&gcm);
  if (rc != 0) return false;
  outLen = ctLen;
  return true;
}

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

void Sha256::finishHex(char out[65]) {
  uint8_t digest[32];
#if MBEDTLS_VERSION_NUMBER >= 0x03000000
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
