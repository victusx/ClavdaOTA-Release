/*
  ---------------------------------------
  ClavdaOTA - Serial Monitor Demo, in C
  ---------------------------------------

  The SerialMonitor example written in C, with the library's C API: the
  sketch is serial_monitor.c, next to this file. Everything printed with
  clavda_serial_printf() and friends shows up in the Clavda console's Serial
  Monitor, from anywhere, over the same connection that carries OTA updates.
  Lines typed there arrive in the clavda_serial_on_message() callback.

  1. Set the device up as in the CloudDemo example: put the clavda_device.h
     from the console next to this sketch, and fill in your Wi-Fi in
     serial_monitor.c.
  2. Flash it, then in the console: Devices -> Connect -> Serial Monitor.

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
