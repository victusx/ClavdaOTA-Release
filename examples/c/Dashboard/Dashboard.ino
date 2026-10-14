/*
  -------------------------------------
  ClavdaOTA - Dashboard Demo, in C
  -------------------------------------

  The Dashboard example written in C, with the library's C API: the sketch
  is dashboard.c, next to this file. Set it up as the C++ one:

  1. Put the clavda_device.h from the console (Devices -> Manage -> Get
     Device File) next to the sketch, and fill in your Wi-Fi in dashboard.c.
  2. Flash it, then in the console: Devices -> Connect -> Dashboard -> Edit.
     The IDs it sends are listed there, ready to add as cards.

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
