/*
  ---------------------------------------------
  ClavdaOTA - Serial Monitor Demo, for ESP-IDF
  ---------------------------------------------

  Everything printed with clavda_serial_printf() and friends shows up in the
  Clavda console's Serial Monitor, from anywhere, over the same connection
  that carries OTA updates. Lines typed there arrive in onMessage(). With the
  logs captured, ESP_LOGx output - yours, the library's and the system's -
  goes there too.

  1. Set the device up as in the CloudDemo example: put the clavda_device.h
     from the console next to this file, and fill in your Wi-Fi below.
  2. idf.py set-target <chip>, then idf.py build flash monitor, once over USB.
  3. In the console: Devices -> Connect -> Serial Monitor. Type "status".

  The last 2 KB of output stays on the device, so a monitor that opens late
  still starts with what the board printed as it booted.

  Works with: ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6
*/

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Your application's version. Letters, digits and . _ + - only, up to 31 characters.
#define CLAVDA_FIRMWARE_VERSION "1.0.0"

#include <ClavdaOTA.h>  // from a .c file: the C API
#if __has_include("clavda_device.h")
  #include "clavda_device.h"
#else
  // Placeholder so the example compiles. The device stays unprovisioned until
  // it is built with the clavda_device.h you download from the console.
  #include "clavda_device.example.h"
#endif

static const char *TAG = "app";

static const char *ssid = "";
static const char *password = "";

/** The signal of the network joined, or 0 while not connected. */
static int rssi(void) {
  wifi_ap_record_t ap;
  return esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
}

// A line typed in the console, without its line ending.
static void onMessage(const char *line, size_t len, void *user) {
  (void)len;
  (void)user;
  if (strcmp(line, "status") == 0) {
    clavda_serial_printf("Firmware %s, free heap %u bytes, RSSI %d dBm\n",
                         clavda_firmware_version(),
                         (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), rssi());
  } else {
    clavda_serial_printf("Received: %s (try \"status\")\n", line);
  }
}

void app_main(void) {
  // Prints go to the USB console as well; the rate is the one in menuconfig.
  clavda_serial_begin(115200, 0);
  // ESP_LOGx output shows in the console's Serial Monitor too.
  clavda_serial_capture_logs(true);
  clavda_serial_on_message(onMessage, NULL);

  // Sets up NVS, esp_netif, the event loop and the Wi-Fi driver, and stays connected.
  clavda_wifi_begin(ssid, password);

  if (!clavda_begin(&CLAVDA_DEVICE)) {
    clavda_serial_println("No device credentials: download clavda_device.h from the console.");
  } else {
    clavda_serial_printf("Device %s running firmware %s\n", clavda_device_id(),
                         clavda_firmware_version());
  }

  int64_t reportedAt = 0;  // esp_timer_get_time(): microseconds since boot
  for (;;) {
    clavda_loop();

    const int64_t now = esp_timer_get_time();
    if (now - reportedAt > 2000000) {
      reportedAt = now;
      clavda_serial_printf("Uptime %lus, %u monitor(s) watching\n",
                           (unsigned long)(now / 1000000), (unsigned)clavda_serial_monitors());
      // A log line, to show it arriving through the captured logs.
      ESP_LOGI(TAG, "Free heap %u bytes", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    }

    // Lets the idle task run, so the task watchdog stays quiet.
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
