/*
  The Dashboard example in C: the same cards, chart and controls as
  examples/cpp/Dashboard, through the C API (clavda_*). Each widget is created
  with the ID of a card or chart in the console, which it then feeds.

  A widget's value goes to every open dashboard only when it changes, so
  setting it on every loop costs nothing. Controls (toggles, sliders, text)
  need permission to edit devices; viewers see the values but cannot use them.
*/

#include <Arduino.h>  // millis(), pinMode(), digitalWrite(), temperatureRead()

#define CLAVDA_FIRMWARE_VERSION "1.0.0"

#include <ClavdaOTA.h>  // from a .c file: the C API
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

static const char *ssid = "";
static const char *password = "";

// One per card or chart, named by its ID in the console.
static clavda_widget_t *temperature;  // Value or Gauge card, in °C
static clavda_widget_t *history;      // Line chart
static clavda_widget_t *uptime;       // Value card, in seconds
static clavda_widget_t *status;       // Status card
static clavda_widget_t *led;          // Toggle
static clavda_widget_t *brightness;   // Slider, 0 to 255
static clavda_widget_t *message;      // Text input

static unsigned long sampledAt = 0;

// The widget already holds the new value when these run, and every open
// dashboard shows it; set it again to refuse or correct it.

static void onLed(clavda_widget_t *widget, const clavda_input_t *in, void *user) {
  (void)widget;
  (void)user;
  const bool on = clavda_input_as_bool(in);
  digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
  clavda_widget_set_status(status, on ? "LED on" : "LED off", on ? CLAVDA_SUCCESS : CLAVDA_NEUTRAL);
}

static void onBrightness(clavda_widget_t *widget, const clavda_input_t *in, void *user) {
  (void)widget;
  (void)user;
  clavda_serial_printf("Brightness %ld\n", clavda_input_as_int(in));
}

static void onMessage(clavda_widget_t *widget, const clavda_input_t *in, void *user) {
  (void)widget;
  (void)user;
  char reply[80];
  snprintf(reply, sizeof(reply), "Got: %s", clavda_input_as_text(in));
  clavda_serial_printf("The dashboard says: %s\n", clavda_input_as_text(in));
  clavda_widget_set_status(status, reply, CLAVDA_INFO);
}

void app_setup(void) {
  // Everything printed also goes to Serial (the USB port).
  clavda_serial_begin(115200, 0);
  pinMode(LED_BUILTIN, OUTPUT);

  temperature = clavda_widget_create("temperature");
  history = clavda_widget_create("temp_history");
  uptime = clavda_widget_create("uptime");
  status = clavda_widget_create("status");
  led = clavda_widget_create("led");
  brightness = clavda_widget_create("brightness");
  message = clavda_widget_create("message");

  clavda_widget_on_change(led, onLed, NULL);
  clavda_widget_on_change(brightness, onBrightness, NULL);
  clavda_widget_on_change(message, onMessage, NULL);

  // Starting values, so a dashboard that opens now has something to show.
  clavda_widget_set_bool(led, false);
  clavda_widget_set_int(brightness, 128);
  clavda_widget_set_status(status, "Starting", CLAVDA_INFO);

  clavda_wifi_begin(ssid, password);

  if (!clavda_begin(&CLAVDA_DEVICE)) {
    clavda_serial_println("No device credentials: download clavda_device.h from the console.");
  } else {
    clavda_widget_set_status(status, "Running", CLAVDA_SUCCESS);
  }
}

void app_loop(void) {
  clavda_loop();

  if (millis() - sampledAt >= 2000) {
    sampledAt = millis();
    // The chip's own sensor: a few degrees above the room, but it moves.
    const float celsius = temperatureRead();
    clavda_widget_set_float(temperature, celsius);
    clavda_widget_add_point(history, celsius);
    clavda_widget_set_int(uptime, (long)(millis() / 1000));
  }
}
