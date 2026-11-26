#include "ClavdaPlatform.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "soc/soc_caps.h"
#if ESP_IDF_VERSION_MAJOR >= 5
  #include "esp_chip_info.h"
  #include "esp_mac.h"
  #include "esp_random.h"
  #include "esp_sntp.h"
#else
  #include "lwip/apps/sntp.h"
#endif
#if __has_include("esp_private/esp_clk.h")
  #include "esp_private/esp_clk.h"
  #define CLAVDA_HAVE_CLK 1
#endif
#if SOC_WIFI_SUPPORTED
  #include "esp_wifi.h"
#endif

namespace clavda {

void sleepMs(uint32_t ms) {
  vTaskDelay(pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1);
}

uint32_t randomBelow(uint32_t n) {
  return n ? esp_random() % n : 0;
}

std::string vformat(const char *fmt, va_list args) {
  char small[128];
  va_list again;
  va_copy(again, args);
  const int len = vsnprintf(small, sizeof(small), fmt, args);
  std::string out;
  if (len > 0 && (size_t)len < sizeof(small)) {
    out.assign(small, len);
  } else if (len > 0) {
    out.resize(len);
    vsnprintf(&out[0], len + 1, fmt, again);
  }
  va_end(again);
  return out;
}

std::string format(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  std::string out = vformat(fmt, args);
  va_end(args);
  return out;
}

bool startsWith(const std::string &s, const char *prefix) {
  return s.compare(0, strlen(prefix), prefix) == 0;
}

bool equalsIgnoreCase(const char *a, const char *b) {
  if (!a || !b) return a == b;
  for (; *a && *b; a++, b++) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
  }
  return *a == *b;
}

/** The interface traffic goes out on. NULL before the network stack is up. */
static esp_netif_t *defaultNetif() {
#if ESP_IDF_VERSION_MAJOR >= 5
  return esp_netif_get_default_netif();
#else
  // IDF 4.4 (Arduino-ESP32 2.x): the first interface that is up.
  for (esp_netif_t *n = esp_netif_next(nullptr); n; n = esp_netif_next(n)) {
    if (esp_netif_is_netif_up(n)) return n;
  }
  return nullptr;
#endif
}

bool networkUp() {
  esp_netif_t *netif = defaultNetif();
  if (!netif || !esp_netif_is_netif_up(netif)) return false;
  esp_netif_ip_info_t ip;
  return esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0;
}

int wifiRssi() {
#if SOC_WIFI_SUPPORTED
  wifi_ap_record_t ap;
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) return ap.rssi;
#endif
  return 0;
}

std::string ipAddress() {
  esp_netif_t *netif = defaultNetif();
  esp_netif_ip_info_t ip;
  if (!netif || esp_netif_get_ip_info(netif, &ip) != ESP_OK) return "0.0.0.0";
  return format(IPSTR, IP2STR(&ip.ip));
}

std::string macAddress() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  return format("%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
                mac[5]);
}

/** CONFIG_IDF_TARGET as a model name: "esp32s3" -> "ESP32-S3". */
static const char *modelName() {
  static char name[16] = {0};
  if (!name[0]) {
    const char *target = CONFIG_IDF_TARGET;
    size_t o = 0;
    for (size_t i = 0; target[i] && o + 2 < sizeof(name); i++) {
      if (i == 5) name[o++] = '-';
      name[o++] = (char)toupper((unsigned char)target[i]);
    }
    name[o] = 0;
  }
  return name;
}

void chipInfo(ChipInfo &info) {
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  info.model = modelName();
  info.revision = chip.revision;
  info.cores = chip.cores;
#ifdef CLAVDA_HAVE_CLK
  info.cpuMHz = (uint32_t)(esp_clk_cpu_freq() / 1000000);
#else
  info.cpuMHz = 0;
#endif
  uint32_t flash = 0;
  if (esp_flash_get_size(nullptr, &flash) != ESP_OK) flash = 0;
  info.flashSize = flash;
  info.psramSize = (uint32_t)heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
}

uint32_t freeHeap() {
  return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
}

uint32_t minFreeHeap() {
  return (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
}

void startClock() {
#if ESP_IDF_VERSION_MAJOR >= 5
  // The esp_sntp_* calls hand their work to the TCP/IP task, so they are safe
  // with the core locking the Arduino core and newer ESP-IDF builds turn on.
  if (esp_sntp_enabled()) return;
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_setservername(1, "time.google.com");
  esp_sntp_init();
#else
  if (sntp_enabled()) return;
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_setservername(0, (char *)"pool.ntp.org");
  sntp_setservername(1, (char *)"time.google.com");
  sntp_init();
#endif
}

bool nvsReady() {
  static bool ready = false;
  if (ready) return true;
  const esp_err_t err = nvs_flash_init();
  // Already initialised by the application, the Wi-Fi driver or the Arduino core.
  if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
    ready = true;
    return true;
  }
  CLAVDA_LOGE("NVS is not usable (%s); it cannot hold this board's identity. Erase the flash "
              "once to reformat it.",
              esp_err_to_name(err));
  return false;
}

void confirmRunningImage() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (!running || esp_ota_get_state_partition(running, &state) != ESP_OK) return;
  if (state != ESP_OTA_IMG_PENDING_VERIFY) return;
  if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
    CLAVDA_LOGI("This image reached the relay; marked valid, rollback cancelled");
  }
}

}  // namespace clavda
