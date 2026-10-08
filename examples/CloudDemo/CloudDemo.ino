/*
  -----------------------
  ClavdaOTA - Cloud Demo
  -----------------------

  Connects an ESP32 to the Clavda relay so it can be updated from the Clavda
  console, from anywhere - no open port, no local network access needed.

  1. In the console: Devices -> Add Device, installation platform "ESP32".
  2. Manage -> Download clavda_device.h, and put it next to this sketch.
  3. Fill in your Wi-Fi below. Bump CLAVDA_FIRMWARE_VERSION for every build
     you upload - the console uses it to tell that an update landed.
  4. Flash over USB once. From then on: Connect -> Update firmware.

  To build one image for several devices (a fleet image), compile with
  CLAVDA_FLEET_BUILD defined: the device credentials are left out and each
  board keeps its own identity from NVS.

  Works with: ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6
*/

#include <WiFi.h>

// Your application's version. Letters, digits and . _ + - only, up to 31 characters.
#define CLAVDA_FIRMWARE_VERSION "1.0.0"

#include <ClavdaOTA.h>
#if __has_include("clavda_device.h")
  #include "clavda_device.h"
#else
  // Placeholder so the example compiles. The device stays unprovisioned until
  // it is built with the clavda_device.h you download from the console.
  #include "clavda_device.example.h"
#endif

const char *ssid = "";
const char *password = "";

unsigned long progressPrintedAt = 0;
bool wifiReported = false;
bool clavdaReported = false;

// Free heap after each step, to see what Wi-Fi and the Clavda connection each take.
void printHeap(const char *step) {
  Serial.printf("[heap] %-18s %u bytes free\n", step, (unsigned)ESP.getFreeHeap());
}

void setup() {
  Serial.begin(115200);
  printHeap("at boot");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  ClavdaOTA.onStart([]() {
    Serial.println("Update started");
  });
  ClavdaOTA.onProgress([](size_t current, size_t final) {
    if (millis() - progressPrintedAt > 1000) {
      progressPrintedAt = millis();
      Serial.printf("Update progress: %u of %u bytes\n", current, final);
    }
  });
  ClavdaOTA.onEnd([](bool success) {
    Serial.println(success ? "Update written, rebooting" : "Update failed");
  });

  // Return false here to put an update off, e.g. while the device is busy.
  ClavdaOTA.onUpdateRequest([](const ClavdaUpdateRequest &request) {
    Serial.printf("Update %s requested (%u bytes)\n", request.version, request.size);
    return true;
  });

  if (!ClavdaOTA.begin(CLAVDA_DEVICE)) {
    Serial.println("No device credentials: download clavda_device.h from the console.");
  } else {
    Serial.printf("Device %s running firmware %s\n", ClavdaOTA.deviceId(), ClavdaOTA.firmwareVersion());
  }
}

void loop() {
  ClavdaOTA.loop();

  if (!wifiReported && WiFi.status() == WL_CONNECTED) {
    wifiReported = true;
    printHeap("Wi-Fi connected");
  }
  if (!clavdaReported && ClavdaOTA.status() == CLAVDA_ONLINE) {
    clavdaReported = true;
    printHeap("Clavda connected");
  }
}
