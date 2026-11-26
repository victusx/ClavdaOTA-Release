#ifndef ClavdaPlatform_h
#define ClavdaPlatform_h

/**
 * What the library needs from the chip, the clock and the network, written
 * against ESP-IDF alone. The Arduino core for the ESP32 is built on ESP-IDF,
 * so these same calls build in the Arduino IDE, in PlatformIO and in a plain
 * ESP-IDF project, and no other file of the library needs an Arduino header.
 *
 * Internal: sketches use the API in ClavdaOTA.h.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string>
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "../common/clavda_types.h"

// The Arduino core compiles ESP_LOGx out below errors; its log_x macros
// follow Tools -> Core Debug Level instead, as the library's logs always have.
#if CLAVDA_ARDUINO && __has_include("esp32-hal-log.h")
  #include "esp32-hal-log.h"
  #define CLAVDA_LOGE(format, ...) log_e("[ClavdaOTA] " format, ##__VA_ARGS__)
  #define CLAVDA_LOGW(format, ...) log_w("[ClavdaOTA] " format, ##__VA_ARGS__)
  #define CLAVDA_LOGI(format, ...) log_i("[ClavdaOTA] " format, ##__VA_ARGS__)
  #define CLAVDA_LOGD(format, ...) log_d("[ClavdaOTA] " format, ##__VA_ARGS__)
#else
  #define CLAVDA_LOGE(format, ...) ESP_LOGE("ClavdaOTA", format, ##__VA_ARGS__)
  #define CLAVDA_LOGW(format, ...) ESP_LOGW("ClavdaOTA", format, ##__VA_ARGS__)
  #define CLAVDA_LOGI(format, ...) ESP_LOGI("ClavdaOTA", format, ##__VA_ARGS__)
  #define CLAVDA_LOGD(format, ...) ESP_LOGD("ClavdaOTA", format, ##__VA_ARGS__)
#endif

namespace clavda {

/** Milliseconds since boot, wrapping every 49 days, like Arduino's millis(). */
inline uint32_t nowMs() {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

/** Seconds since boot. */
inline uint32_t uptimeSeconds() {
  return (uint32_t)(esp_timer_get_time() / 1000000);
}

void sleepMs(uint32_t ms);
/** Uniform in [0, n); 0 when n is 0. */
uint32_t randomBelow(uint32_t n);

/** printf into a std::string. */
std::string format(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
std::string vformat(const char *fmt, va_list args);
bool startsWith(const std::string &s, const char *prefix);
bool equalsIgnoreCase(const char *a, const char *b);

/** The default network interface - Wi-Fi or Ethernet - is up and has an address. */
bool networkUp();
/** Signal of the access point the station is on; 0 when not on Wi-Fi. */
int wifiRssi();
/** Address of the default interface, "0.0.0.0" when there is none. */
std::string ipAddress();
/** The Wi-Fi station MAC, "AA:BB:CC:DD:EE:FF". */
std::string macAddress();

struct ChipInfo {
  /** "ESP32", "ESP32-S3", "ESP32-C3", ... */
  const char *model;
  uint16_t revision;
  uint8_t cores;
  uint32_t cpuMHz;
  uint32_t flashSize;
  uint32_t psramSize;
};
void chipInfo(ChipInfo &info);
/** Internal RAM, as Arduino's ESP.getFreeHeap() reports it. */
uint32_t freeHeap();
uint32_t minFreeHeap();

/** Start SNTP, unless the application already runs it. Needs the network up. */
void startClock();

/**
 * NVS, initialised if the application has not done it yet. Never erases the
 * partition: that would also erase this board's identity.
 */
bool nvsReady();

/**
 * With app rollback on, a freshly updated image runs on probation until it
 * is marked valid. Reaching the relay is proof enough: the image can take
 * the next update. Does nothing on an image that is not on probation.
 */
void confirmRunningImage();

}  // namespace clavda

#endif
