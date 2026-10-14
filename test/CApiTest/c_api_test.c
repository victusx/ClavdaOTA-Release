/*
  Checks of the C API, compiled as C. Everything here runs without Wi-Fi or a
  provisioned device: the identity, the bundle a clavda_device.h makes, the
  widgets and the Serial Monitor buffer.
*/

#include <Arduino.h>
#include <math.h>
#include <string.h>

#define CLAVDA_FIRMWARE_VERSION "0.0.0-test"

#include <ClavdaOTA.h>

// A bundle with no credentials, made from C the way the console's clavda_device.h makes one.
CLAVDA_DEFINE_BUNDLE(TEST_DEVICE, "https://relay.invalid", "", "", "", 0, "")

static int passed;
static int failed;

#define CHECK(condition) check((condition), #condition, __LINE__)

static void check(bool ok, const char *what, int line) {
  if (ok) {
    passed++;
  } else {
    failed++;
    clavda_serial_printf("  FAIL line %d: %s\n", line, what);
  }
}

static bool near(double a, double b) {
  return fabs(a - b) < 1e-6;
}

static void test_bundle(void) {
  const ClavdaDescriptor *d = TEST_DEVICE.descriptor;
  CHECK(strcmp(d->magic, CLAVDA_DESCRIPTOR_MAGIC) == 0);
  CHECK(strcmp(d->firmware, "0.0.0-test") == 0);
  CHECK(strcmp(d->chip, CLAVDA_CHIP) == 0);
  CHECK(strcmp(d->library, CLAVDA_LIB_VERSION) == 0);
  CHECK(strcmp(d->bundle, "") == 0);
  CHECK(strcmp(TEST_DEVICE.relayUrl, "https://relay.invalid") == 0);
  CHECK(TEST_DEVICE.generation == 0);
}

static void test_before_begin(void) {
  CHECK(clavda_status() == CLAVDA_UNPROVISIONED);
  CHECK(clavda_begin(NULL) == false);
  CHECK(clavda_device_id() != NULL);
  CHECK(clavda_firmware_version() != NULL);
}

static void test_values(void) {
  clavda_widget_t *w = clavda_widget_create("c_test_value");
  CHECK(w != NULL);
  CHECK(strcmp(clavda_widget_id(w), "c_test_value") == 0);

  clavda_widget_set_number(w, 21.5);
  CHECK(near(clavda_widget_number(w), 21.5));
  CHECK(clavda_widget_is_on(w));

  clavda_widget_set_float(w, 0.25f);
  CHECK(near(clavda_widget_number(w), 0.25));

  clavda_widget_set_int(w, -7);
  CHECK(near(clavda_widget_number(w), -7));

  clavda_widget_set_int(w, 0);
  CHECK(!clavda_widget_is_on(w));

  clavda_widget_set_bool(w, true);
  CHECK(clavda_widget_is_on(w));
  CHECK(near(clavda_widget_number(w), 1));
  clavda_widget_set_bool(w, false);
  CHECK(!clavda_widget_is_on(w));

  clavda_widget_set_text(w, "42.5");
  CHECK(strcmp(clavda_widget_text(w), "42.5") == 0);
  CHECK(near(clavda_widget_number(w), 42.5));

  clavda_widget_set_status(w, "Door open", CLAVDA_WARNING);
  CHECK(strcmp(clavda_widget_text(w), "Door open") == 0);

  clavda_widget_set_text(w, NULL);
  CHECK(strcmp(clavda_widget_text(w), "") == 0);

  clavda_widget_free(w);
}

static void test_long_text(void) {
  // CLAVDA_WIDGET_MAX_TEXT is 256 unless the build changes it.
  char text[400];
  memset(text, 'a', sizeof(text) - 1);
  text[sizeof(text) - 1] = 0;
  clavda_widget_t *w = clavda_widget_create("c_test_long");
  clavda_widget_set_text(w, text);
  CHECK(strlen(clavda_widget_text(w)) == 256);
  clavda_widget_free(w);
}

static void test_charts(void) {
  static const char *const labels[] = {"Mon", "Tue", "Wed"};
  static const float values[] = {1.5f, 2.25f, 3.0f};
  clavda_widget_t *line = clavda_widget_create("c_test_line");
  clavda_widget_t *bars = clavda_widget_create("c_test_bars");
  for (int i = 0; i < 10; i++) clavda_widget_add_point(line, (float)i);
  clavda_widget_set_series(bars, labels, values, 3);
  clavda_widget_set_series(bars, NULL, values, 3);
  CHECK(line != NULL && bars != NULL);
  clavda_widget_free(line);
  clavda_widget_free(bars);
}

static int changes;

static void on_change(clavda_widget_t *widget, const clavda_input_t *input, void *user) {
  (void)widget;
  (void)input;
  (void)user;
  changes++;
}

static void test_callbacks(void) {
  clavda_widget_t *w = clavda_widget_create("c_test_toggle");
  clavda_widget_on_change(w, on_change, &changes);
  clavda_widget_on_change(w, NULL, NULL);
  clavda_widget_on_change(w, on_change, NULL);
  // A widget freed with a callback set must leave nothing behind.
  clavda_widget_free(w);
  CHECK(changes == 0);

  clavda_on_update_request(NULL, NULL);
  clavda_on_update_start(NULL, NULL);
  clavda_on_update_progress(NULL, NULL);
  clavda_on_update_end(NULL, NULL);
  clavda_serial_on_message(NULL, NULL);
  CHECK(true);
}

static void test_null_safety(void) {
  clavda_widget_set_number(NULL, 1);
  clavda_widget_set_float(NULL, 1);
  clavda_widget_set_int(NULL, 1);
  clavda_widget_set_bool(NULL, true);
  clavda_widget_set_text(NULL, "x");
  clavda_widget_set_status(NULL, "x", CLAVDA_INFO);
  clavda_widget_add_point(NULL, 1);
  clavda_widget_set_series(NULL, NULL, NULL, 0);
  clavda_widget_on_change(NULL, on_change, NULL);
  clavda_widget_free(NULL);
  CHECK(strcmp(clavda_widget_id(NULL), "") == 0);
  CHECK(clavda_widget_number(NULL) == 0);
  CHECK(!clavda_widget_is_on(NULL));
  CHECK(strcmp(clavda_widget_text(NULL), "") == 0);

  CHECK(!clavda_input_is_bool(NULL));
  CHECK(!clavda_input_is_number(NULL));
  CHECK(!clavda_input_is_text(NULL));
  CHECK(!clavda_input_as_bool(NULL));
  CHECK(clavda_input_as_int(NULL) == 0);
  CHECK(clavda_input_as_double(NULL) == 0);
  CHECK(strcmp(clavda_input_as_text(NULL), "") == 0);

  CHECK(clavda_serial_write(NULL, 5) == 0);
  CHECK(clavda_serial_print(NULL) == 0);
}

static void test_serial(void) {
  CHECK(clavda_serial_write("ab", 2) == 2);
  CHECK(clavda_serial_printf("%d-%s", 42, "x") == 4);
  // Longer than the 128-byte line printf formats on the stack.
  char wide[300];
  memset(wide, 'z', sizeof(wide) - 1);
  wide[sizeof(wide) - 1] = 0;
  CHECK(clavda_serial_printf("[%s]", wide) == sizeof(wide) + 1);
  CHECK(clavda_serial_print("\n") == 1);
  CHECK(clavda_serial_monitors() == 0);
}

int run_c_api_tests(void) {
  passed = 0;
  failed = 0;
  CHECK(clavda_serial_begin(115200, 1024));
  clavda_serial_println("ClavdaOTA C API tests");

  test_bundle();
  test_before_begin();
  test_values();
  test_long_text();
  test_charts();
  test_callbacks();
  test_null_safety();
  test_serial();

  clavda_serial_printf("C API: %d passed, %d failed\n", passed, failed);
  clavda_serial_println(failed ? "FAILED" : "ALL PASSED");
  return failed;
}
