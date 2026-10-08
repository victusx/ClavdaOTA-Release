/*
  --------------------------------
  ClavdaOTA - Serial Monitor Demo
  --------------------------------

  Everything printed to ClavdaSerial shows up in the Clavda console's Serial
  Monitor, from anywhere, over the same connection that carries OTA updates.
  Lines typed there arrive in ClavdaSerial.onMessage().

  1. Set the device up as in the CloudDemo example: put the clavda_device.h
     from the console next to this sketch, and fill in your Wi-Fi below.
  2. Flash it, then in the console: Devices -> Connect -> Serial Monitor.

  Print to ClavdaSerial wherever you would print to Serial. Passing &Serial to
  ClavdaSerial.begin() mirrors everything to USB as well, and the last 2 KB of
  output is kept for monitors that open later - including the boot messages.

  Works with: ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6
*/

#include <WiFi.h>

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

unsigned long reportedAt = 0;

void setup() {
  Serial.begin(115200);
  ClavdaSerial.begin(&Serial);

  ClavdaSerial.onMessage([](const String &msg) {
    if (msg == "status") {
      ClavdaSerial.printf("Firmware %s, free heap %u bytes, RSSI %d dBm\n",
                          ClavdaOTA.firmwareVersion(), (unsigned)ESP.getFreeHeap(), WiFi.RSSI());
    } else {
      ClavdaSerial.printf("Received: %s (try \"status\")\n", msg.c_str());
    }
  });

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  if (!ClavdaOTA.begin(CLAVDA_DEVICE)) {
    ClavdaSerial.println("No device credentials: download clavda_device.h from the console.");
  } else {
    ClavdaSerial.printf("Device %s running firmware %s\n", ClavdaOTA.deviceId(),
                        ClavdaOTA.firmwareVersion());
  }
}

void loop() {
  ClavdaOTA.loop();

  if (millis() - reportedAt > 2000) {
    reportedAt = millis();
    ClavdaSerial.printf("Uptime %lus, %u monitor(s) watching\n", millis() / 1000,
                        (unsigned)ClavdaSerial.monitors());
  }
}
