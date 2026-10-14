/*
  ClavdaOTA C++ API tests. Everything here runs without Wi-Fi or a
  provisioned device. Flash it and open the serial monitor at 115200: the
  last line is ALL PASSED or FAILED. See test/README.md.
*/

#include <math.h>
#include <string.h>

#define CLAVDA_FIRMWARE_VERSION "0.0.0-test"

#include <ClavdaOTA.h>

CLAVDA_DEFINE_BUNDLE(TEST_DEVICE, "https://relay.invalid", "", "", "", 0, "")

static int passed = 0;
static int failed = 0;

#define CHECK(condition) check((condition), #condition, __LINE__)

static void check(bool ok, const char *what, int line) {
  if (ok) {
    passed++;
  } else {
    failed++;
    ClavdaSerial.printf("  FAIL line %d: %s\n", line, what);
  }
}

static bool near(double a, double b) {
  return fabs(a - b) < 1e-6;
}

// Globals, as a sketch declares them: constructed before setup().
ClavdaWidget value("cpp_test_value");
ClavdaWidget toggle("cpp_test_toggle");

static void testBundle() {
  const ClavdaDescriptor *d = TEST_DEVICE.descriptor;
  CHECK(strcmp(d->magic, CLAVDA_DESCRIPTOR_MAGIC) == 0);
  CHECK(strcmp(d->firmware, "0.0.0-test") == 0);
  CHECK(strcmp(d->chip, CLAVDA_CHIP) == 0);
  CHECK(strcmp(d->library, CLAVDA_LIB_VERSION) == 0);
}

static void testBeforeBegin() {
  CHECK(ClavdaOTA.status() == CLAVDA_UNPROVISIONED);
  CHECK(ClavdaOTA.deviceId() != nullptr);
}

static void testValues() {
  CHECK(strcmp(value.id(), "cpp_test_value") == 0);
  value.set(21.5);
  CHECK(near(value.number(), 21.5));
  value.set(0.25f);
  CHECK(near(value.number(), 0.25));
  value.set(-7);
  CHECK(near(value.number(), -7));
  value.set(3000000000UL);
  CHECK(near(value.number(), 3000000000.0));
  value.set(String("42.5"));
  CHECK(strcmp(value.text(), "42.5") == 0);
  CHECK(near(value.number(), 42.5));
  value.set("Door open", CLAVDA_WARNING);
  CHECK(strcmp(value.text(), "Door open") == 0);

  toggle.set(true);
  CHECK(toggle.isOn());
  toggle.set(false);
  CHECK(!toggle.isOn());
}

static void testCharts() {
  static const char *const labels[] = {"Mon", "Tue", "Wed"};
  static const float values[] = {1.5f, 2.25f, 3.0f};
  ClavdaWidget line("cpp_test_line");
  ClavdaWidget bars("cpp_test_bars");
  for (int i = 0; i < 10; i++) line.add((float)i);
  bars.setSeries(labels, values, 3);
  bars.setSeries(values, 3);
  CHECK(true);
}  // both go out of scope here and leave the dashboard

static void testCallbacks() {
  toggle.onChange([](const ClavdaInput &in) { (void)in; });
  toggle.onChange(nullptr);
  ClavdaOTA.onUpdateRequest([](const ClavdaUpdateRequest &r) { return r.size > 0; });
  ClavdaOTA.onUpdateRequest(nullptr);
  ClavdaSerial.onMessage([](const String &msg) { (void)msg; });
  ClavdaSerial.onMessage([](uint8_t *data, size_t len) {
    (void)data;
    (void)len;
  });
  CHECK(true);
}

// The C API works from C++ too, next to the C++ one.
static void testCApiFromCpp() {
  clavda_widget_t *w = clavda_widget_create("cpp_test_c_widget");
  CHECK(w != nullptr);
  clavda_widget_set_status(w, "From C", CLAVDA_INFO);
  CHECK(strcmp(clavda_widget_text(w), "From C") == 0);
  clavda_widget_free(w);
  CHECK(clavda_status() == ClavdaOTA.status());
}

static void testSerial() {
  CHECK(ClavdaSerial.print("ab") == 2);
  CHECK(ClavdaSerial.printf("%d-%s", 42, "x") == 4);
  CHECK(ClavdaSerial.monitors() == 0);
  ClavdaSerial.println();
}

void setup() {
  Serial.begin(115200);
  delay(1500);  // time for the serial monitor to attach
  CHECK(ClavdaSerial.begin(&Serial, 1024));
  ClavdaSerial.println("ClavdaOTA C++ API tests");

  testBundle();
  testBeforeBegin();
  testValues();
  testCharts();
  testCallbacks();
  testCApiFromCpp();
  testSerial();

  ClavdaSerial.printf("C++ API: %d passed, %d failed\n", passed, failed);
  ClavdaSerial.println(failed ? "FAILED" : "ALL PASSED");
}

void loop() {
  delay(1000);
}
