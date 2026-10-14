/*
  The CloudDemo example in C: connect to the Clavda relay, report each update
  as it happens, and print the free heap after each step of starting up.

  Output goes through the library's Serial Monitor functions, which also start
  Serial (the USB port) and print there, so it shows on USB and in the
  console's Serial Monitor alike.
*/

#include <Arduino.h>  // millis()
#include "esp_heap_caps.h"

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

static unsigned long progressPrintedAt = 0;
static bool wifiReported = false;
static bool clavdaReported = false;

// Free heap after each step, to see what Wi-Fi and the Clavda connection each take.
// With PSRAM turned on, most of it comes out of PSRAM instead of the internal heap.
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
  if (millis() - progressPrintedAt > 1000) {
    progressPrintedAt = millis();
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

void app_setup(void) {
  // Starts Serial at 115200 too; the 2 KB buffer it takes is in the first heap figure.
  clavda_serial_begin(115200, 0);
  printHeap("at boot");

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
}

void app_loop(void) {
  clavda_loop();

  if (!wifiReported && clavda_wifi_connected()) {
    wifiReported = true;
    printHeap("Wi-Fi connected");
  }
  if (!clavdaReported && clavda_status() == CLAVDA_ONLINE) {
    clavdaReported = true;
    printHeap("Clavda connected");
  }
}
