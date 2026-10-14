#include "ClavdaSession.h"

#include <ArduinoJson.h>
#include <time.h>

#include "ClavdaCrypto.h"

namespace clavda {

// A clock before this has not been set by SNTP yet.
static const time_t CLOCK_VALID_AFTER = 1700000000;

bool openSession(const char *kind, const char *what, const char *sessionId, const char *enc,
                 Identity &identity, SessionGrant &grant, const char *&code, String &why) {
  code = "invalid";
  if (!isValidId(sessionId, 64)) {
    why = "Malformed session id";
    return false;
  }

  String plain;
  String error;
  if (!openJob(identity.jobKey().c_str(), enc, plain, error)) {
    why = String("The ") + what + " did not verify on this device";
    return false;
  }
  JsonDocument sealed;
  if (deserializeJson(sealed, plain)) {
    why = String("Malformed ") + what;
    return false;
  }

  const char *sealedKind = sealed["kind"] | "";
  const char *sealedId = sealed["sessionId"] | "";
  const char *sealedDevice = sealed["deviceId"] | "";
  if ((sealed["v"] | 0) != 1 || strcmp(sealedKind, kind)) {
    why = String("This ") + what + " needs a newer ClavdaOTA library";
    return false;
  }
  if (strcmp(sealedId, sessionId) || !identity.deviceId().equals(sealedDevice)) {
    why = String("This ") + what + " is for another device";
    return false;
  }

  const time_t now = time(nullptr);
  const uint32_t expires = sealed["exp"] | 0u;
  if (now > CLOCK_VALID_AFTER && expires && now > (time_t)expires) {
    code = "expired";
    why = String("This ") + what + " has expired";
    return false;
  }

  uint8_t key[36];
  size_t keyLen = 0;
  if (!base64Decode(sealed["key"] | "", key, sizeof(key), keyLen) || keyLen != 32) {
    memset(key, 0, sizeof(key));
    why = String("The ") + what + " carries no valid key";
    return false;
  }
  memcpy(grant.key, key, sizeof(grant.key));
  memset(key, 0, sizeof(key));
  grant.input = sealed["input"] | false;
  return true;
}

}  // namespace clavda
