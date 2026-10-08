#include "ClavdaIdentity.h"

#include <Preferences.h>
#include "ClavdaCrypto.h"

namespace clavda {

static const char *NVS_NAMESPACE = "clavda";
// Must fit the descriptor's bundle[64] field, NUL included.
static const size_t MAX_DEVICE_ID = 63;
static const size_t MAX_TOKEN = 2048;

/** A bundle that could actually identify a device - not a fleet build's empty one. */
static bool usable(const char *deviceId, const char *token, const char *jobKey) {
  return isValidId(deviceId, MAX_DEVICE_ID) && token && token[0] && strlen(token) <= MAX_TOKEN &&
         isJobKey(jobKey);
}

bool Identity::resolve(const ClavdaBundle &bundle) {
  Preferences nvs;
  if (!nvs.begin(NVS_NAMESPACE, false)) {
    log_e("[ClavdaOTA] NVS is not available; cannot load the device identity");
    return false;
  }

  String storedId = nvs.getString("id", "");
  String storedToken = nvs.getString("tok", "");
  String storedKey = nvs.getString("key", "");
  uint32_t storedGen = nvs.getUInt("gen", 0);

  const bool haveStored = usable(storedId.c_str(), storedToken.c_str(), storedKey.c_str());
  const bool haveCompiled = usable(bundle.deviceId, bundle.token, bundle.jobKey);

  if (!haveStored) {
    if (!haveCompiled) {
      nvs.end();
      return false;
    }
    // First boot of a per-device image: this is where the board gets its identity.
    nvs.putString("id", bundle.deviceId);
    nvs.putString("tok", bundle.token);
    nvs.putString("key", bundle.jobKey);
    nvs.putUInt("gen", bundle.generation);
    storedId = bundle.deviceId;
    storedToken = bundle.token;
    storedKey = bundle.jobKey;
    storedGen = bundle.generation;
    log_i("[ClavdaOTA] Identity %s seeded from the image (generation %u)", storedId.c_str(),
          (unsigned)storedGen);
  } else if (haveCompiled) {
    if (storedId.equals(bundle.deviceId)) {
      if (bundle.generation > storedGen) {
        nvs.putString("tok", bundle.token);
        nvs.putString("key", bundle.jobKey);
        nvs.putUInt("gen", bundle.generation);
        storedToken = bundle.token;
        storedKey = bundle.jobKey;
        storedGen = bundle.generation;
        log_i("[ClavdaOTA] Credentials rotated to generation %u", (unsigned)storedGen);
      }
    } else {
      _mismatch = true;
      log_w("[ClavdaOTA] This image was built for device %s, but this board is %s. Keeping %s. "
            "Erase flash to re-provision.",
            bundle.deviceId, storedId.c_str(), storedId.c_str());
    }
  }

  nvs.end();
  _deviceId = storedId;
  _token = storedToken;
  _jobKey = storedKey;
  _generation = storedGen;
  return true;
}

String Identity::lastJobId() {
  Preferences nvs;
  if (!nvs.begin(NVS_NAMESPACE, true)) return String();
  String id = nvs.getString("lastJob", "");
  nvs.end();
  return id;
}

void Identity::setPendingJob(const String &jobId, const String &targetVersion) {
  Preferences nvs;
  if (!nvs.begin(NVS_NAMESPACE, false)) return;
  nvs.putString("lastJob", jobId);
  nvs.putString("pendJob", jobId);
  nvs.putString("pendVer", targetVersion);
  nvs.end();
}

bool Identity::takePendingJob(String &jobId, String &targetVersion) {
  Preferences nvs;
  if (!nvs.begin(NVS_NAMESPACE, false)) return false;
  jobId = nvs.getString("pendJob", "");
  targetVersion = nvs.getString("pendVer", "");
  if (jobId.length()) {
    nvs.remove("pendJob");
    nvs.remove("pendVer");
  }
  nvs.end();
  return jobId.length() > 0;
}

}  // namespace clavda
