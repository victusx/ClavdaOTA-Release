/*
  ----------------------------------------
  ClavdaOTA - Dashboard Demo, for ESP-IDF
  ----------------------------------------

  The same cards, chart and controls as the Arduino Dashboard examples,
  through the C API (clavda_*). Each widget is created with the ID of a card
  or chart in the console, which it then feeds.

  1. Put the clavda_device.h from the console (Devices -> Manage -> Get
     Device File) next to this file, and fill in your Wi-Fi below.
  2. idf.py set-target <chip>, then idf.py build flash monitor, once over USB.
  3. In the console: Devices -> Connect -> Dashboard -> Edit. The IDs this
     sends are listed there, ready to add as cards.

  A widget's value goes to every open dashboard only when it changes, so
  setting it on every pass costs nothing. Controls (toggles, sliders, text)
  need permission to edit devices; viewers see the values but cannot use them.

  Works with: ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6. The original
  ESP32 has no temperature sensor in ESP-IDF, so there the temperature card
  and chart stay empty.
*/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"
#if SOC_TEMP_SENSOR_SUPPORTED
  #include "driver/temperature_sensor.h"
#endif

// Your application's version. Letters, digits and . _ + - only, up to 31 characters.
#define CLAVDA_FIRMWARE_VERSION "1.0.0"

#include <ClavdaOTA.h>  // from a .c file: the C API
#if __has_include("clavda_device.h")
  #include "clavda_device.h"
#else
  // Placeholder so the example compiles. The device stays unprovisioned until
  // it is built with the clavda_device.h you download from the console.
  #include "clavda_device.example.h"
#endif

// The LED the toggle drives. GPIO 2 on most ESP32 dev boards; change it for yours.
#define LED_GPIO GPIO_NUM_2

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

// The widget already holds the new value when these run, and every open
// dashboard shows it; set it again to refuse or correct it.

static void onLed(clavda_widget_t *widget, const clavda_input_t *in, void *user) {
  (void)widget;
  (void)user;
  const bool on = clavda_input_as_bool(in);
  gpio_set_level(LED_GPIO, on ? 1 : 0);
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

void app_main(void) {
  // Everything printed also goes to the USB console.
  clavda_serial_begin(115200, 0);

  gpio_reset_pin(LED_GPIO);
  gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

#if SOC_TEMP_SENSOR_SUPPORTED
  // The chip's own sensor: a few degrees above the room, but it moves.
  temperature_sensor_handle_t sensor = NULL;
  temperature_sensor_config_t sensorConfig = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
  if (temperature_sensor_install(&sensorConfig, &sensor) == ESP_OK) {
    temperature_sensor_enable(sensor);
  }
#endif

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

  // Sets up NVS, esp_netif, the event loop and the Wi-Fi driver, and stays connected.
  clavda_wifi_begin(ssid, password);

  if (!clavda_begin(&CLAVDA_DEVICE)) {
    clavda_serial_println("No device credentials: download clavda_device.h from the console.");
  } else {
    clavda_widget_set_status(status, "Running", CLAVDA_SUCCESS);
  }

  int64_t sampledAt = 0;  // esp_timer_get_time(): microseconds since boot
  for (;;) {
    clavda_loop();

    const int64_t now = esp_timer_get_time();
    if (now - sampledAt >= 2000000) {
      sampledAt = now;
#if SOC_TEMP_SENSOR_SUPPORTED
      float celsius = 0;
      if (sensor && temperature_sensor_get_celsius(sensor, &celsius) == ESP_OK) {
        clavda_widget_set_float(temperature, celsius);
        clavda_widget_add_point(history, celsius);
      }
#endif
      clavda_widget_set_int(uptime, (long)(now / 1000000));
    }

    // Lets the idle task run, so the task watchdog stays quiet.
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
