/*
  ClavdaOTA C API tests. The checks are in c_api_test.c, compiled as C; this
  file only starts them. Flash it and open the serial monitor at 115200: the
  last line is ALL PASSED or FAILED. See test/README.md.
*/

extern "C" int run_c_api_tests(void);

void setup() {
  // No Serial.begin(): clavda_serial_begin() starts it, as it does for C sketches.
  delay(1500);  // time for the serial monitor to attach
  run_c_api_tests();
}

void loop() {
  delay(1000);
}
