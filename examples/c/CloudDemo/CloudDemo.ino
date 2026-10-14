/*
  ---------------------------------
  ClavdaOTA - Cloud Demo, in C
  ---------------------------------

  The CloudDemo example written in C, with the library's C API: the sketch is
  cloud_demo.c, next to this file. It connects an ESP32 to the Clavda relay so
  it can be updated from the Clavda console, from anywhere - no open port, no
  local network access needed.

  1. In the console: Devices -> Add Device, installation platform "ESP32".
  2. Manage -> Download clavda_device.h, and put it next to this sketch.
  3. Fill in your Wi-Fi in cloud_demo.c. Bump CLAVDA_FIRMWARE_VERSION for
     every build you upload - the console uses it to tell that an update landed.
  4. Flash over USB once. From then on: Connect -> Update firmware.

  To build one image for several devices (a fleet image), compile with
  CLAVDA_FLEET_BUILD defined: the device credentials are left out and each
  board keeps its own identity from NVS.

  Arduino starts every sketch through setup() and loop(), which it calls as
  C++, so this file only hands them over to the C code.

  Works with: ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6
*/

extern "C" void app_setup(void);
extern "C" void app_loop(void);

void setup() {
  app_setup();
}

void loop() {
  app_loop();
}
