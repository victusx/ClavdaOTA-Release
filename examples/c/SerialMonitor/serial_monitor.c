/*
  The SerialMonitor example in C. Print with clavda_serial_printf() wherever
  you would print to Serial: clavda_serial_begin(115200, ...) also starts
  Serial and mirrors everything to USB, and the last 2 KB of output is kept
  for monitors that open later - including the boot messages.
*/

#include <Arduino.h>  // millis()
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_wifi.h"

#define CLAVDA_FIRMWARE_VERSION "1.0.0"

#include <ClavdaOTA.h>  // from a .c file: the C API
#if __has_include("clavda_device.h")
  #include "clavda_device.h"
#else
  // Placeholder so the example compiles. The device stays unprovisioned until
  // it is built with the clavda_device.h you download from the console.
  #include "clavda_device.example.h"
#endif

static const char *ssid = "";
static const char *password = "";

static unsigned long reportedAt = 0;

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

void app_setup(void) {
  clavda_serial_begin(115200, 0);
  clavda_serial_on_message(onMessage, NULL);

  clavda_wifi_begin(ssid, password);

  if (!clavda_begin(&CLAVDA_DEVICE)) {
    clavda_serial_println("No device credentials: download clavda_device.h from the console.");
  } else {
    clavda_serial_printf("Device %s running firmware %s\n", clavda_device_id(),
                         clavda_firmware_version());
  }
}

void app_loop(void) {
  clavda_loop();

  if (millis() - reportedAt > 2000) {
    reportedAt = millis();
    clavda_serial_printf("Uptime %lus, %u monitor(s) watching\n", millis() / 1000,
                         (unsigned)clavda_serial_monitors());
  }
}
