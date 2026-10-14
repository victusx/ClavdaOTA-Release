/*
  ----------------------------
  ClavdaOTA - Dashboard Demo
  ----------------------------

  A live dashboard for this board in the Clavda console: cards, gauges,
  charts, and controls that call back into the sketch - from anywhere, over
  the same end-to-end encrypted connection that carries OTA updates.

  The dashboard is built in the console, not here. Each card or chart there
  has an ID, and a ClavdaWidget in the sketch with the same ID feeds it:

  1. Set the device up as in the CloudDemo example: put the clavda_device.h
     from the console next to this sketch, and fill in your Wi-Fi below.
  2. Flash it, then in the console: Devices -> Connect -> Dashboard -> Edit.
     The IDs this sketch sends are listed there, ready to add as cards - or
     build your own and copy each widget's line from its settings.

  set() sends a value only when it changes, so calling it on every loop()
  costs nothing. Controls (toggles, sliders, buttons, text) need permission
  to edit devices; viewers see the values but cannot use them.

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

#ifndef LED_BUILTIN
  #define LED_BUILTIN 2
#endif

const char *ssid = "";
const char *password = "";

// One per card or chart, named by its ID in the console.
ClavdaWidget temperature("temperature");  // Value or Gauge card, in °C
ClavdaWidget history("temp_history");     // Line chart
ClavdaWidget uptime("uptime");            // Value card, in seconds
ClavdaWidget status("status");            // Status card
ClavdaWidget led("led");                  // Toggle
ClavdaWidget brightness("brightness");    // Slider, 0 to 255
ClavdaWidget message("message");          // Text input

unsigned long sampledAt = 0;

void setup() {
  Serial.begin(115200);
  ClavdaSerial.begin(&Serial);
  pinMode(LED_BUILTIN, OUTPUT);

  // The widget already holds the new value when these run, and every open
  // dashboard shows it; set() it again to refuse or correct it.
  led.onChange([](const ClavdaInput &in) {
    digitalWrite(LED_BUILTIN, in.asBool() ? HIGH : LOW);
    status.set(in.asBool() ? "LED on" : "LED off", in.asBool() ? CLAVDA_SUCCESS : CLAVDA_NEUTRAL);
  });
  brightness.onChange([](const ClavdaInput &in) {
    ClavdaSerial.printf("Brightness %ld\n", in.asInt());
  });
  message.onChange([](const ClavdaInput &in) {
    ClavdaSerial.printf("The dashboard says: %s\n", in.asText());
    status.set(String("Got: ") + in.asText(), CLAVDA_INFO);
  });

  // Starting values, so a dashboard that opens now has something to show.
  led.set(false);
  brightness.set(128);
  status.set("Starting", CLAVDA_INFO);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  if (!ClavdaOTA.begin(CLAVDA_DEVICE)) {
    ClavdaSerial.println("No device credentials: download clavda_device.h from the console.");
  } else {
    status.set("Running", CLAVDA_SUCCESS);
  }
}

void loop() {
  ClavdaOTA.loop();

  if (millis() - sampledAt >= 2000) {
    sampledAt = millis();
    // The chip's own sensor: a few degrees above the room, but it moves.
    const float celsius = temperatureRead();
    temperature.set(celsius);
    history.add(celsius);
    uptime.set(millis() / 1000);
  }
}
