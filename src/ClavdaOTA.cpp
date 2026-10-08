#include "ClavdaOTA.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <time.h>
#include "esp_idf_version.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#if ESP_IDF_VERSION_MAJOR >= 5
  #include "esp_sntp.h"
#else
  #include "lwip/apps/sntp.h"
#endif

#include "ClavdaCrypto.h"
#include "ClavdaIdentity.h"
#include "ClavdaLink.h"
#include "ClavdaMonitor.h"
#include "ClavdaPull.h"

using namespace clavda;

namespace {

const uint32_t HEARTBEAT_MS = 30000;
// Faster while updating, so a console opened mid-update catches up quickly.
const uint32_t HEARTBEAT_UPDATING_MS = 5000;
const uint32_t PROGRESS_MS = 1000;
// Long enough for the result frame to leave before the radio goes down.
const uint32_t REBOOT_GRACE_MS = 1500;
// A clock before this has not been set by SNTP yet.
const time_t CLOCK_VALID_AFTER = 1700000000;

struct State {
  Identity identity;
  Link link;
  Pull pull;
  Monitor monitor;

  const ClavdaBundle *bundle = nullptr;
  String firmwareVersion;
  ClavdaStatus status = CLAVDA_UNPROVISIONED;

  String otaJobId;  // the job in flight, if any
  Pull::Phase reportedPhase = Pull::IDLE;
  uint32_t lastProgress = 0;
  uint32_t lastHeartbeat = 0;
  uint32_t rebootAt = 0;

  std::function<bool(const ClavdaUpdateRequest &)> onUpdateRequest;
  std::function<void()> onStart;
  std::function<void(size_t, size_t)> onProgress;
  std::function<void(bool)> onEnd;
};

State st;

/** The descriptor's version rule. Must match FIRMWARE_VERSION_PATTERN in the console. */
bool validFirmwareVersion(const char *v) {
  if (!v || !v[0]) return false;
  size_t len = 0;
  for (; v[len]; len++) {
    const char c = v[len];
    const bool alnum = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    if (len == 0 ? !alnum : !(alnum || c == '.' || c == '_' || c == '+' || c == '-')) return false;
    if (len >= 31) return false;
  }
  return true;
}

bool isHex64(const char *s) {
  if (!s || strlen(s) != 64) return false;
  for (size_t i = 0; i < 64; i++) {
    if (!isxdigit((unsigned char)s[i])) return false;
  }
  return true;
}

bool sntpRunning() {
#if ESP_IDF_VERSION_MAJOR >= 5
  return esp_sntp_enabled();
#else
  return sntp_enabled();
#endif
}

const char *resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_EXT: return "external";
    default: return "other";
  }
}

/** Relay phase names for a pull phase. */
const char *otaPhaseName(Pull::Phase phase) {
  switch (phase) {
    case Pull::CONNECTING: return "accepted";
    case Pull::DOWNLOADING: return "downloading";
    case Pull::VERIFYING: return "verifying";
    case Pull::DONE: return "rebooting";
    default: return "accepted";
  }
}

String authJson() {
  JsonDocument doc;
  doc["token"] = st.identity.token();
  doc["clientVersion"] = CLAVDA_LIB_VERSION;
  doc["firmwareVersion"] = st.firmwareVersion;
  doc["kind"] = "mcu";
  doc["chip"] = CLAVDA_CHIP;
  JsonObject bundle = doc["bundle"].to<JsonObject>();
  bundle["gen"] = st.identity.generation();
  bundle["mismatch"] = st.identity.mismatch();
  String out;
  serializeJson(doc, out);
  return out;
}

void sendToWeb(const char *type, const char *requestId, JsonDocument &payload) {
  JsonDocument msg;
  msg["type"] = type;
  if (requestId && requestId[0]) msg["requestId"] = requestId;
  msg["payload"] = payload;
  String out;
  serializeJson(msg, out);
  st.link.emit("send_to_web", out);
}

uint8_t otaPercent() {
  const size_t total = st.pull.job().size;
  return total ? (uint8_t)((uint64_t)st.pull.written() * 100 / total) : 0;
}

void sendHeartbeat() {
  if (!st.link.online()) return;
  JsonDocument doc;
  JsonObject metrics = doc["metrics"].to<JsonObject>();
  metrics["heap"] = ESP.getFreeHeap();
  metrics["heapMin"] = ESP.getMinFreeHeap();
  metrics["rssi"] = WiFi.RSSI();
  metrics["uptime"] = (uint32_t)(esp_timer_get_time() / 1000000);
  doc["clientVersion"] = CLAVDA_LIB_VERSION;
  doc["firmwareVersion"] = st.firmwareVersion;
  doc["kind"] = "mcu";
  doc["chip"] = CLAVDA_CHIP;
  JsonObject bundle = doc["bundle"].to<JsonObject>();
  bundle["gen"] = st.identity.generation();
  bundle["mismatch"] = st.identity.mismatch();
  if (st.otaJobId.length()) {
    JsonObject ota = doc["ota"].to<JsonObject>();
    ota["jobId"] = st.otaJobId;
    ota["phase"] = otaPhaseName(st.pull.phase());
    ota["pct"] = otaPercent();
  }
  String out;
  serializeJson(doc, out);
  st.link.emit("heartbeat", out);
  st.lastHeartbeat = millis();
}

void reportProgress(bool force) {
  const Pull::Phase phase = st.pull.phase();
  if (!force && millis() - st.lastProgress < PROGRESS_MS) return;
  JsonDocument payload;
  payload["jobId"] = st.otaJobId;
  payload["phase"] = otaPhaseName(phase);
  payload["written"] = st.pull.written();
  payload["total"] = st.pull.job().size;
  sendToWeb("ota_progress", nullptr, payload);
  st.lastProgress = millis();
  st.reportedPhase = phase;
}

void sendResult(bool ok, const String &error) {
  JsonDocument payload;
  payload["jobId"] = st.otaJobId;
  payload["ok"] = ok;
  payload["version"] = st.pull.job().version;
  if (!ok) payload["error"] = error;
  sendToWeb("ota_result", nullptr, payload);
}

void fillDeviceInfo(JsonDocument &reply) {
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
  reply["success"] = true;
  reply["kind"] = "mcu";
  reply["chip"] = CLAVDA_CHIP;
  reply["chipModel"] = ESP.getChipModel();
  reply["chipRevision"] = ESP.getChipRevision();
  reply["cores"] = ESP.getChipCores();
  reply["cpuFreqMHz"] = ESP.getCpuFreqMHz();
  reply["flashSize"] = ESP.getFlashChipSize();
  reply["runningPartition"] = running ? running->label : "";
  reply["otaSlotSize"] = next ? next->size : 0;
  reply["heapFree"] = ESP.getFreeHeap();
  reply["heapMin"] = ESP.getMinFreeHeap();
  reply["psramSize"] = ESP.getPsramSize();
  reply["rssi"] = WiFi.RSSI();
  reply["ip"] = WiFi.localIP().toString();
  reply["mac"] = WiFi.macAddress();
  reply["uptimeSeconds"] = (uint32_t)(esp_timer_get_time() / 1000000);
  reply["firmwareVersion"] = st.firmwareVersion;
  reply["clientVersion"] = CLAVDA_LIB_VERSION;
  reply["bundleGeneration"] = st.identity.generation();
  reply["bundleMismatch"] = st.identity.mismatch();
  reply["idfVersion"] = esp_get_idf_version();
  reply["resetReason"] = resetReasonName(esp_reset_reason());
  reply["serialMonitor"] = st.monitor.available();
}

/**
 * Validate a sealed job and, if it is acceptable, arm the download. Every
 * refusal is answered with a reason the console can show as-is.
 */
void handleOtaUpdate(JsonVariantConst payload, JsonDocument &reply) {
  auto reject = [&reply](const String &why) {
    reply["success"] = false;
    reply["state"] = "rejected";
    reply["error"] = why;
  };

  if (st.pull.busy() || st.rebootAt) return reject("An update is already running");

  String plain;
  String error;
  const char *sealed = payload["enc"] | "";
  if (!openJob(st.identity.jobKey().c_str(), sealed, plain, error)) return reject(error);

  JsonDocument job;
  if (deserializeJson(job, plain)) return reject("Malformed job");
  if ((job["v"] | 0) != 1) return reject("This job needs a newer ClavdaOTA library");

  const char *jobDevice = job["deviceId"] | "";
  const char *jobId = job["jobId"] | "";
  const char *url = job["url"] | "";
  const char *sha256 = job["sha256"] | "";
  const char *version = job["version"] | "";
  const char *chip = job["chip"] | "";
  const uint32_t size = job["size"] | 0u;
  const uint32_t expires = job["exp"] | 0u;

  if (!st.identity.deviceId().equals(jobDevice)) return reject("This job is for another device");
  if (!isValidId(jobId, 64)) return reject("Malformed job id");

  // Plain http is only tolerated against a local, plain-http relay.
  const String relay = st.bundle->relayUrl;
  const bool relayIsTls = relay.startsWith("https://") || relay.startsWith("wss://");
  const bool urlIsTls = !strncmp(url, "https://", 8);
  if (!urlIsTls && (relayIsTls || strncmp(url, "http://", 7))) {
    return reject("The download address must be https");
  }
  if (!isHex64(sha256)) return reject("The job carries no valid SHA-256");
  if (!validFirmwareVersion(version)) return reject("The job carries no valid version");
  if (strcmp(chip, CLAVDA_CHIP)) {
    return reject(String("This build is for ") + chip + "; this board is " + CLAVDA_CHIP);
  }

  const time_t now = time(nullptr);
  // Without a clock, the presigned link's own expiry still bounds a replay.
  if (now > CLOCK_VALID_AFTER && expires && now > (time_t)expires) {
    return reject("This update job has expired; start it again from the console");
  }
  if (st.identity.lastJobId().equals(jobId)) return reject("This job has already been installed");
  if (st.firmwareVersion.equals(version)) return reject(String("Already running ") + version);

  const esp_partition_t *slot = esp_ota_get_next_update_partition(nullptr);
  if (!slot) return reject("This partition table has no OTA slot");
  if (size == 0 || size > slot->size) {
    return reject(String("The image (") + size + " bytes) does not fit the " + slot->size +
                  "-byte OTA slot");
  }

  if (st.onUpdateRequest) {
    const ClavdaUpdateRequest request = {jobId, version, size};
    if (!st.onUpdateRequest(request)) {
      reply["success"] = true;
      reply["state"] = "deferred";
      reply["jobId"] = jobId;
      reply["reason"] = "The application asked to update later";
      return;
    }
  }

  Pull::Job pullJob;
  pullJob.jobId = jobId;
  pullJob.url = url;
  pullJob.sha256 = sha256;
  pullJob.version = version;
  pullJob.size = size;
  if (!st.pull.start(pullJob, st.bundle->caRoots)) return reject("Could not start the download");

  st.otaJobId = jobId;
  st.reportedPhase = Pull::IDLE;
  st.lastProgress = 0;
  if (st.onStart) st.onStart();
  log_i("[ClavdaOTA] Update %s accepted: %s -> %s (%u bytes)", jobId, st.firmwareVersion.c_str(),
        version, (unsigned)size);

  reply["success"] = true;
  reply["state"] = "accepted";
  reply["jobId"] = jobId;
}

void onRelayEvent(const char *event, JsonVariantConst data) {
  if (strcmp(event, "command")) return;

  const char *command = data["command"] | "";
  const char *requestId = data["requestId"] | "";
  JsonVariantConst payload = data["payload"];
  JsonDocument reply;

  if (!strcmp(command, "ping")) {
    reply["success"] = true;
    reply["pong"] = true;
  } else if (!strcmp(command, "get_device_info")) {
    fillDeviceInfo(reply);
  } else if (!strcmp(command, "reboot_device")) {
    if (st.pull.busy()) {
      reply["success"] = false;
      reply["error"] = "An update is running; reboot after it finishes";
    } else {
      reply["success"] = true;
      st.rebootAt = millis() + REBOOT_GRACE_MS;
    }
  } else if (!strcmp(command, "ota_update")) {
    handleOtaUpdate(payload, reply);
  } else if (st.monitor.handle(command, payload, st.identity, reply)) {
    // serial_start / serial_input / serial_stop
  } else if (!strcmp(command, "ota_cancel")) {
    const char *jobId = payload["jobId"] | "";
    if (st.pull.busy() && st.otaJobId.equals(jobId)) {
      st.pull.abort("Cancelled from the console");
      reply["success"] = true;
    } else {
      reply["success"] = false;
      reply["error"] = "No such update is running";
    }
  } else {
    reply["success"] = false;
    reply["error"] = String("Unknown command: ") + command + " (not supported on ESP32)";
  }

  if (requestId[0]) sendToWeb("response", requestId, reply);
}

void onRelayOnline() {
  sendHeartbeat();
  // A console that opened while the link was down learns where the update is.
  if (st.otaJobId.length()) reportProgress(true);
}

}  // namespace

bool ClavdaOTAClass::begin(const ClavdaBundle &bundle) {
  if (st.bundle) return st.status != CLAVDA_UNPROVISIONED;
  st.bundle = &bundle;

  st.firmwareVersion = bundle.descriptor ? String(bundle.descriptor->firmware) : String();
  if (!validFirmwareVersion(st.firmwareVersion.c_str())) {
    log_e("[ClavdaOTA] CLAVDA_FIRMWARE_VERSION \"%s\" is not a valid version; the console cannot "
          "track updates without one",
          st.firmwareVersion.c_str());
  }

  if (!st.identity.resolve(bundle)) {
    st.status = CLAVDA_UNPROVISIONED;
    log_e("[ClavdaOTA] No device identity. Download clavda_device.h from the console and flash "
          "an image built with it (not a fleet build) once over USB.");
    return false;
  }

  String pendingJob;
  String pendingVersion;
  if (st.identity.takePendingJob(pendingJob, pendingVersion)) {
    if (pendingVersion.equals(st.firmwareVersion)) {
      log_i("[ClavdaOTA] Update %s installed; running %s", pendingJob.c_str(),
            st.firmwareVersion.c_str());
    } else {
      log_w("[ClavdaOTA] Update %s expected %s, but %s booted", pendingJob.c_str(),
            pendingVersion.c_str(), st.firmwareVersion.c_str());
    }
  }

  // Needed for job expiry. Left alone if the sketch already runs SNTP.
  if (!sntpRunning()) configTime(0, 0, "pool.ntp.org", "time.google.com");

  st.pull.onProgress([](size_t current, size_t total) {
    if (st.onProgress) st.onProgress(current, total);
  });

  if (!st.link.begin(bundle.relayUrl ? bundle.relayUrl : "", bundle.caRoots, authJson,
                     onRelayEvent, onRelayOnline)) {
    st.status = CLAVDA_CONNECTING;
    return false;
  }

  st.status = CLAVDA_CONNECTING;
  log_i("[ClavdaOTA] Device %s, firmware %s, library " CLAVDA_LIB_VERSION,
        st.identity.deviceId().c_str(), st.firmwareVersion.c_str());
  return true;
}

void ClavdaOTAClass::loop() {
  if (!st.bundle) return;

  st.link.loop();
  st.pull.loop();

  if (st.otaJobId.length() && !st.rebootAt) {
    const Pull::Phase phase = st.pull.phase();
    if (phase == Pull::DONE) {
      st.identity.setPendingJob(st.otaJobId, st.pull.job().version);
      reportProgress(true);
      sendResult(true, "");
      if (st.onEnd) st.onEnd(true);
      log_i("[ClavdaOTA] Update %s written; rebooting into %s", st.otaJobId.c_str(),
            st.pull.job().version.c_str());
      st.rebootAt = millis() + REBOOT_GRACE_MS;
    } else if (phase == Pull::FAILED) {
      sendResult(false, st.pull.error());
      if (st.onEnd) st.onEnd(false);
      st.otaJobId = "";
      st.pull.reset();
      sendHeartbeat();
    } else {
      reportProgress(phase != st.reportedPhase);
    }
  }

  st.monitor.pump(st.link);

  const uint32_t interval = st.otaJobId.length() ? HEARTBEAT_UPDATING_MS : HEARTBEAT_MS;
  if (st.link.online() && millis() - st.lastHeartbeat >= interval) sendHeartbeat();

  if (st.rebootAt && (int32_t)(millis() - st.rebootAt) >= 0) {
    st.link.stop();
    delay(100);
    ESP.restart();
  }

  if (st.otaJobId.length() || st.rebootAt) {
    st.status = CLAVDA_UPDATING;
  } else if (st.link.rejected()) {
    st.status = CLAVDA_REJECTED;
  } else if (st.link.online()) {
    st.status = CLAVDA_ONLINE;
  } else {
    st.status = CLAVDA_CONNECTING;
  }
}

void ClavdaOTAClass::onUpdateRequest(std::function<bool(const ClavdaUpdateRequest &)> callable) {
  st.onUpdateRequest = callable;
}

void ClavdaOTAClass::onStart(std::function<void()> callable) {
  st.onStart = callable;
}

void ClavdaOTAClass::onProgress(std::function<void(size_t current, size_t final)> callable) {
  st.onProgress = callable;
}

void ClavdaOTAClass::onEnd(std::function<void(bool success)> callable) {
  st.onEnd = callable;
}

ClavdaStatus ClavdaOTAClass::status() const {
  return st.status;
}

const char *ClavdaOTAClass::deviceId() const {
  return st.identity.deviceId().c_str();
}

const char *ClavdaOTAClass::firmwareVersion() const {
  return st.firmwareVersion.c_str();
}

bool ClavdaOTAClass::bundleMismatch() const {
  return st.identity.mismatch();
}

ClavdaOTAClass ClavdaOTA;
