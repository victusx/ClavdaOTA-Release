#ifndef ClavdaCrypto_h
#define ClavdaCrypto_h

#include <stddef.h>
#include <stdint.h>
#include <string>
#include "mbedtls/version.h"
// mbedTLS 4 (ESP-IDF 6) keeps its hash and cipher contexts private; the PSA
// Crypto API is the public way in there. Older versions keep the classic API.
#if MBEDTLS_VERSION_NUMBER >= 0x04000000
  #define CLAVDA_PSA_CRYPTO 1
  #include "psa/crypto.h"
#else
  #include "mbedtls/sha256.h"
#endif

namespace clavda {

/** `^[A-Za-z0-9_-]{1,max}$` - the relay's id rule, so an id it accepts, we accept. */
bool isValidId(const char *id, size_t max);

/** Standard base64 into `out`. False on malformed input or if `out` is too small. */
bool base64Decode(const char *in, uint8_t *out, size_t outCap, size_t &outLen);

/** Base64 of `len` bytes into `out`, NUL-terminated. False if `out` is too small. */
bool base64Encode(const uint8_t *data, size_t len, char *out, size_t outCap, size_t &outLen);

/** Bytes base64Encode() writes for `len` bytes, without the NUL. */
inline size_t base64Length(size_t len) {
  return 4 * ((len + 2) / 3);
}

/** True if `keyB64` decodes to a 32-byte AES-256 key. */
bool isJobKey(const char *keyB64);

/**
 * Open an OTA job sealed by the console: base64(iv[12] | ciphertext | tag[16])
 * under the device's AES-256 job key - the framing the headless agent already
 * uses for SSH credentials. `plain` is filled only when the tag verifies.
 */
bool openJob(const char *jobKeyB64, const char *sealedB64, std::string &plain,
             std::string &error);

/** What sealBytes() adds to the plaintext: the IV in front, the tag behind. */
const size_t SEAL_OVERHEAD = 12 + 16;

/**
 * Serial monitor traffic, in the same framing as a job but under a session's
 * own key, with `aad` bound into the tag so a frame cannot be replayed at
 * another position in the stream. Writes iv | ciphertext | tag - `len` +
 * SEAL_OVERHEAD bytes - to `out`, which must not overlap `plain`.
 */
bool sealBytes(const uint8_t key[32], const uint8_t *plain, size_t len, const char *aad,
               uint8_t *out);
/** Open into `out` (`outCap` bytes). False unless the tag verifies with `aad`. */
bool openBytes(const uint8_t key[32], const char *sealedB64, const char *aad, uint8_t *out,
               size_t outCap, size_t &outLen);

/**
 * On a board with PSRAM, point mbedTLS's allocator at it. The core pins every
 * mbedTLS allocation to internal RAM, so each TLS connection - two 16 KB record
 * buffers, the parsed CA roots and the server's chain - otherwise comes out of
 * the same heap as the sketch. Applies to every TLS connection in the sketch.
 * False, with nothing changed, when there is no PSRAM.
 */
bool tlsPreferPsram();

class Sha256 {
  public:
    Sha256();
    ~Sha256();
    void begin();
    void update(const uint8_t *data, size_t len);
    /** Lowercase hex digest, 64 characters plus NUL. */
    void finishHex(char out[65]);

  private:
#ifdef CLAVDA_PSA_CRYPTO
    psa_hash_operation_t _op;
#else
    mbedtls_sha256_context _ctx;
#endif
};

}  // namespace clavda

#endif
