/*
  ------------------------------------
  ClavdaOTA - Cloud Demo, for ESP-IDF
  ------------------------------------

  Connects an ESP32 to the Clavda relay so it can be updated from the Clavda
  console, from anywhere - no open port, no local network access needed. The
  same library as the Arduino examples, called through its C API from
  app_main(); a main.cpp could use the C++ API (ClavdaOTA, ClavdaSerial,
  ClavdaWidget) just the same.

  1. In the console: Devices -> Add Device, installation platform "ESP32".
  2. Manage -> Download clavda_device.h, and put it next to this file.
  3. Fill in your Wi-Fi below. Bump CLAVDA_FIRMWARE_VERSION for every build
     you upload - the console uses it to tell that an update landed.
  4. idf.py set-target <chip>, then idf.py build flash monitor, once over USB.
     From then on: Connect -> Update firmware, with build/clavda_cloud_demo.bin.

  To build one image for several devices (a fleet image), define
  CLAVDA_FLEET_BUILD above the clavda_device.h include: the device
  credentials are left out and each board keeps its own identity from NVS.

  Works with: ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6
*/

#include <stdbool.h>
#include <stdint.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
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

static const char *ssid = "";
static const char *password = "";

static int64_t progressPrintedAt = 0;

// Free heap after each step, to see what Wi-Fi and the Clavda connection each take.
static void printHeap(const char *step) {
  clavda_serial_printf("[heap] %-18s %u bytes free, PSRAM %u bytes free\n", step,
                       (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                       (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void onStart(void *user) {
  (void)user;
  clavda_serial_println("Update started");
}

static void onProgress(size_t current, size_t total, void *user) {
  (void)user;
  const int64_t now = esp_timer_get_time();
  if (now - progressPrintedAt > 1000000) {
    progressPrintedAt = now;
    clavda_serial_printf("Update progress: %u of %u bytes\n", (unsigned)current, (unsigned)total);
  }
}

static void onEnd(bool success, void *user) {
  (void)user;
  clavda_serial_println(success ? "Update written, rebooting" : "Update failed");
}

// Return false here to put an update off, e.g. while the device is busy.
static bool onUpdateRequest(const ClavdaUpdateRequest *request, void *user) {
  (void)user;
  clavda_serial_printf("Update %s requested (%u bytes)\n", request->version,
                       (unsigned)request->size);
  return true;
}

void app_main(void) {
  // Prints go to the USB console too; with the logs captured, ESP_LOGx output
  // (the library's own, and the system's) shows in the console's Serial Monitor.
  clavda_serial_begin(115200, 0);
  clavda_serial_capture_logs(true);
  printHeap("at boot");

  // Sets up NVS, esp_netif, the event loop and the Wi-Fi driver, and stays connected.
  clavda_wifi_begin(ssid, password);

  clavda_on_update_start(onStart, NULL);
  clavda_on_update_progress(onProgress, NULL);
  clavda_on_update_end(onEnd, NULL);
  clavda_on_update_request(onUpdateRequest, NULL);

  if (!clavda_begin(&CLAVDA_DEVICE)) {
    clavda_serial_println("No device credentials: download clavda_device.h from the console.");
  } else {
    clavda_serial_printf("Device %s running firmware %s\n", clavda_device_id(),
                         clavda_firmware_version());
  }

  bool wifiReported = false;
  bool clavdaReported = false;
  for (;;) {
    clavda_loop();

    if (!wifiReported && clavda_wifi_connected()) {
      wifiReported = true;
      printHeap("Wi-Fi connected");
    }
    if (!clavdaReported && clavda_status() == CLAVDA_ONLINE) {
      clavdaReported = true;
      printHeap("Clavda connected");
    }

    // Lets the idle task run, so the task watchdog stays quiet. Updates still
    // download at full speed: each clavda_loop() reads for up to 50 ms.
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
