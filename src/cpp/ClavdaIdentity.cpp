#include "ClavdaIdentity.h"

#include <string.h>
#include "nvs.h"
#include "ClavdaCrypto.h"
#include "ClavdaPlatform.h"

namespace clavda {

// The namespace and keys Arduino's Preferences wrote in 2.2 and earlier, with
// the same NVS types: strings, and a u32 for the generation.
static const char *NVS_NAMESPACE = "clavda";
// Must fit the descriptor's bundle[64] field, NUL included.
static const size_t MAX_DEVICE_ID = 63;
static const size_t MAX_TOKEN = 2048;

/** A bundle that could actually identify a device - not a fleet build's empty one. */
static bool usable(const char *deviceId, const char *token, const char *jobKey) {
  return isValidId(deviceId, MAX_DEVICE_ID) && token && token[0] && strlen(token) <= MAX_TOKEN &&
         isJobKey(jobKey);
}

/** NVS opened on the library's namespace, closed when it goes out of scope. */
class Nvs {
  public:
    explicit Nvs(bool write) {
      if (!nvsReady()) return;
      _open = nvs_open(NVS_NAMESPACE, write ? NVS_READWRITE : NVS_READONLY, &_h) == ESP_OK;
    }
    ~Nvs() {
      if (_open) nvs_close(_h);
    }
    bool ok() const { return _open; }

    std::string getString(const char *key) {
      size_t len = 0;
      if (!_open || nvs_get_str(_h, key, nullptr, &len) != ESP_OK || len < 2) return std::string();
      std::string value(len, '\0');
      if (nvs_get_str(_h, key, &value[0], &len) != ESP_OK) return std::string();
      value.resize(len - 1);  // len counted the NUL
      return value;
    }
    uint32_t getU32(const char *key) {
      uint32_t value = 0;
      if (!_open || nvs_get_u32(_h, key, &value) != ESP_OK) return 0;
      return value;
    }
    void putString(const char *key, const std::string &value) {
      if (_open) nvs_set_str(_h, key, value.c_str());
    }
    void putU32(const char *key, uint32_t value) {
      if (_open) nvs_set_u32(_h, key, value);
    }
    void remove(const char *key) {
      if (_open) nvs_erase_key(_h, key);
    }
    bool commit() { return _open && nvs_commit(_h) == ESP_OK; }

  private:
    nvs_handle_t _h = 0;
    bool _open = false;
};

bool Identity::resolve(const ClavdaBundle &bundle) {
  Nvs nvs(true);
  if (!nvs.ok()) {
    CLAVDA_LOGE("NVS is not available; cannot load the device identity");
    return false;
  }

  std::string storedId = nvs.getString("id");
  std::string storedToken = nvs.getString("tok");
  std::string storedKey = nvs.getString("key");
  uint32_t storedGen = nvs.getU32("gen");

  const bool haveStored = usable(storedId.c_str(), storedToken.c_str(), storedKey.c_str());
  const bool haveCompiled = usable(bundle.deviceId, bundle.token, bundle.jobKey);

  if (!haveStored) {
    if (!haveCompiled) return false;
    // First boot of a per-device image: this is where the board gets its identity.
    nvs.putString("id", bundle.deviceId);
    nvs.putString("tok", bundle.token);
    nvs.putString("key", bundle.jobKey);
    nvs.putU32("gen", bundle.generation);
    if (!nvs.commit()) {
      CLAVDA_LOGE("Could not write the device identity to NVS");
      return false;
    }
    storedId = bundle.deviceId;
    storedToken = bundle.token;
    storedKey = bundle.jobKey;
    storedGen = bundle.generation;
    CLAVDA_LOGI("Identity %s seeded from the image (generation %u)", storedId.c_str(),
                (unsigned)storedGen);
  } else if (haveCompiled) {
    if (storedId == bundle.deviceId) {
      if (bundle.generation > storedGen) {
        nvs.putString("tok", bundle.token);
        nvs.putString("key", bundle.jobKey);
        nvs.putU32("gen", bundle.generation);
        if (nvs.commit()) {
          storedToken = bundle.token;
          storedKey = bundle.jobKey;
          storedGen = bundle.generation;
          CLAVDA_LOGI("Credentials rotated to generation %u", (unsigned)storedGen);
        }
      }
    } else {
      _mismatch = true;
      CLAVDA_LOGW("This image was built for device %s, but this board is %s. Keeping %s. "
                  "Erase flash to re-provision.",
                  bundle.deviceId, storedId.c_str(), storedId.c_str());
    }
  }

  _deviceId = storedId;
  _token = storedToken;
  _jobKey = storedKey;
  _generation = storedGen;
  return true;
}

std::string Identity::lastJobId() {
  Nvs nvs(false);
  return nvs.getString("lastJob");
}

void Identity::setPendingJob(const std::string &jobId, const std::string &targetVersion) {
  Nvs nvs(true);
  nvs.putString("lastJob", jobId);
  nvs.putString("pendJob", jobId);
  nvs.putString("pendVer", targetVersion);
  nvs.commit();
}

bool Identity::takePendingJob(std::string &jobId, std::string &targetVersion) {
  Nvs nvs(true);
  if (!nvs.ok()) return false;
  jobId = nvs.getString("pendJob");
  targetVersion = nvs.getString("pendVer");
  if (!jobId.empty()) {
    nvs.remove("pendJob");
    nvs.remove("pendVer");
    nvs.commit();
  }
  return !jobId.empty();
}

}  // namespace clavda
