// The C API: each function hands over to the C++ implementation, so the two
// behave the same. Compiled as C++; only its header needs to be C.

#include "clavda_ota.h"

#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../cpp/ClavdaOTA.h"
#include "../cpp/ClavdaPlatform.h"

#if CLAVDA_ARDUINO
  // Serial and the WiFi library: only on the Arduino core, where they exist.
  #include <Arduino.h>
  #include <WiFi.h>
#else
  #include "esp_event.h"
  #include "esp_netif.h"
  #include "soc/soc_caps.h"
  #if SOC_WIFI_SUPPORTED
    #include "esp_wifi.h"
  #endif
#endif

// ---- Updates ----

bool clavda_begin(const ClavdaBundle *bundle) {
  return bundle && ClavdaOTA.begin(*bundle);
}

void clavda_loop(void) {
  ClavdaOTA.loop();
}

ClavdaStatus clavda_status(void) {
  return ClavdaOTA.status();
}

const char *clavda_device_id(void) {
  return ClavdaOTA.deviceId();
}

const char *clavda_firmware_version(void) {
  return ClavdaOTA.firmwareVersion();
}

bool clavda_bundle_mismatch(void) {
  return ClavdaOTA.bundleMismatch();
}

// An empty std::function is what the C++ API takes as "no callback".

void clavda_on_update_request(clavda_update_request_fn callback, void *user) {
  if (!callback) return ClavdaOTA.onUpdateRequest(nullptr);
  ClavdaOTA.onUpdateRequest(
    [callback, user](const ClavdaUpdateRequest &request) { return callback(&request, user); });
}

void clavda_on_update_start(clavda_update_start_fn callback, void *user) {
  if (!callback) return ClavdaOTA.onStart(nullptr);
  ClavdaOTA.onStart([callback, user]() { callback(user); });
}

void clavda_on_update_progress(clavda_update_progress_fn callback, void *user) {
  if (!callback) return ClavdaOTA.onProgress(nullptr);
  ClavdaOTA.onProgress(
    [callback, user](size_t current, size_t total) { callback(current, total, user); });
}

void clavda_on_update_end(clavda_update_end_fn callback, void *user) {
  if (!callback) return ClavdaOTA.onEnd(nullptr);
  ClavdaOTA.onEnd([callback, user](bool success) { callback(success, user); });
}

// ---- Wi-Fi ----

#if CLAVDA_ARDUINO

void clavda_wifi_begin(const char *ssid, const char *password) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
}

#elif SOC_WIFI_SUPPORTED

static void onWifiEvent(void *, esp_event_base_t base, int32_t id, void *) {
  // Connect once the driver is up, and again after every drop.
  if (base == WIFI_EVENT && (id == WIFI_EVENT_STA_START || id == WIFI_EVENT_STA_DISCONNECTED)) {
    esp_wifi_connect();
  }
}

/** Set-up calls the application may already have made are fine to repeat. */
static bool okOrDone(esp_err_t err, const char *what) {
  if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) return true;
  CLAVDA_LOGE("Wi-Fi: %s failed: %s", what, esp_err_to_name(err));
  return false;
}

void clavda_wifi_begin(const char *ssid, const char *password) {
  static bool started = false;
  if (!clavda::nvsReady()) return;
  if (!started) {
    if (!okOrDone(esp_netif_init(), "esp_netif_init") ||
        !okOrDone(esp_event_loop_create_default(), "esp_event_loop_create_default")) {
      return;
    }
    if (!esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")) esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (!okOrDone(esp_wifi_init(&init), "esp_wifi_init")) return;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onWifiEvent, nullptr,
                                        nullptr);
    esp_wifi_set_mode(WIFI_MODE_STA);
  }

  wifi_config_t config = {};
  // A 32-byte SSID or a 64-character key fills its field with no terminator.
  if (ssid) memcpy(config.sta.ssid, ssid, strnlen(ssid, sizeof(config.sta.ssid)));
  if (password) {
    memcpy(config.sta.password, password, strnlen(password, sizeof(config.sta.password)));
  }
  // With a password, an open network of the same name will not do.
  if (password && password[0]) config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  if (!okOrDone(esp_wifi_set_config(WIFI_IF_STA, &config), "esp_wifi_set_config")) return;

  if (!started) {
    started = okOrDone(esp_wifi_start(), "esp_wifi_start");
  } else {
    // Another network: leave this one, and the disconnect handler joins the new one.
    esp_wifi_disconnect();
  }
}

#else

void clavda_wifi_begin(const char *, const char *) {
  CLAVDA_LOGE("This chip has no Wi-Fi; bring the network up with esp_netif yourself");
}

#endif

bool clavda_wifi_connected(void) {
  return clavda::networkUp();
}

// ---- Serial Monitor ----

bool clavda_serial_begin(unsigned long mirror_baud, size_t buffer_size) {
  const size_t size = buffer_size ? buffer_size : CLAVDA_SERIAL_BUFFER_SIZE;
#if CLAVDA_ARDUINO
  // Serial drops writes until it is started, and C cannot start it.
  if (mirror_baud) Serial.begin(mirror_baud);
  return ClavdaSerial.begin(mirror_baud ? &Serial : nullptr, size);
#else
  // ESP-IDF set its console up already, at the rate chosen in menuconfig.
  return ClavdaSerial.begin(mirror_baud != 0, size);
#endif
}

void clavda_serial_capture_logs(bool on) {
  ClavdaSerial.captureLogs(on);
}

size_t clavda_serial_write(const void *data, size_t len) {
  return data ? ClavdaSerial.write((const uint8_t *)data, len) : 0;
}

size_t clavda_serial_print(const char *text) {
  return ClavdaSerial.print(text ? text : "");
}

size_t clavda_serial_println(const char *text) {
  return ClavdaSerial.println(text ? text : "");
}

size_t clavda_serial_printf(const char *format, ...) {
  if (!format) return 0;
  // Most lines fit on the stack; a longer one is formatted again into the heap.
  char line[128];
  va_list args;
  va_start(args, format);
  va_list again;
  va_copy(again, args);
  const int len = vsnprintf(line, sizeof(line), format, args);
  va_end(args);

  size_t written = 0;
  if (len > 0 && (size_t)len < sizeof(line)) {
    written = ClavdaSerial.write((const uint8_t *)line, len);
  } else if (len > 0) {
    char *text = (char *)malloc(len + 1);
    if (text) {
      vsnprintf(text, len + 1, format, again);
      written = ClavdaSerial.write((const uint8_t *)text, len);
      free(text);
    }
  }
  va_end(again);
  return written;
}

void clavda_serial_on_message(clavda_serial_message_fn callback, void *user) {
  if (!callback) return ClavdaSerial.onMessage(ClavdaSerialClass::MessageHandler());
  // The monitor hands over the line NUL-terminated, so it can go to C as text.
  ClavdaSerial.onMessage(ClavdaSerialClass::MessageHandler(
    [callback, user](uint8_t *data, size_t len) { callback((const char *)data, len, user); }));
}

size_t clavda_serial_monitors(void) {
  return ClavdaSerial.monitors();
}

// ---- Dashboard ----

/** The C handle: the widget itself, plus the C callback its onChange calls. */
struct clavda_widget {
  explicit clavda_widget(const char *id) : widget(id) {}
  ClavdaWidget widget;
  clavda_widget_change_fn onChange = nullptr;
  void *user = nullptr;
};

// A clavda_input_t is never defined: it is a ClavdaInput under another name.
static const ClavdaInput *cpp(const clavda_input_t *input) {
  return reinterpret_cast<const ClavdaInput *>(input);
}

clavda_widget_t *clavda_widget_create(const char *id) {
  return new (std::nothrow) clavda_widget(id);
}

void clavda_widget_free(clavda_widget_t *widget) {
  delete widget;
}

void clavda_widget_set_number(clavda_widget_t *widget, double value) {
  if (widget) widget->widget.set(value);
}

void clavda_widget_set_float(clavda_widget_t *widget, float value) {
  if (widget) widget->widget.set(value);
}

void clavda_widget_set_int(clavda_widget_t *widget, long value) {
  if (widget) widget->widget.set(value);
}

void clavda_widget_set_bool(clavda_widget_t *widget, bool on) {
  if (widget) widget->widget.set(on);
}

void clavda_widget_set_text(clavda_widget_t *widget, const char *text) {
  if (widget) widget->widget.set(text);
}

void clavda_widget_set_status(clavda_widget_t *widget, const char *text, ClavdaLevel level) {
  if (widget) widget->widget.set(text, level);
}

void clavda_widget_add_point(clavda_widget_t *widget, float value) {
  if (widget) widget->widget.add(value);
}

void clavda_widget_set_series(clavda_widget_t *widget, const char *const labels[],
                              const float values[], size_t count) {
  if (widget) widget->widget.setSeries(labels, values, count);
}

void clavda_widget_on_change(clavda_widget_t *widget, clavda_widget_change_fn callback,
                             void *user) {
  if (!widget) return;
  widget->onChange = callback;
  widget->user = user;
  if (!callback) return widget->widget.onChange(nullptr);
  widget->widget.onChange([widget](const ClavdaInput &input) {
    widget->onChange(widget, reinterpret_cast<const clavda_input_t *>(&input), widget->user);
  });
}

const char *clavda_widget_id(const clavda_widget_t *widget) {
  return widget ? widget->widget.id() : "";
}

double clavda_widget_number(const clavda_widget_t *widget) {
  return widget ? widget->widget.number() : 0;
}

bool clavda_widget_is_on(const clavda_widget_t *widget) {
  return widget && widget->widget.isOn();
}

const char *clavda_widget_text(const clavda_widget_t *widget) {
  return widget ? widget->widget.text() : "";
}

bool clavda_input_is_bool(const clavda_input_t *input) {
  return input && cpp(input)->isBool();
}

bool clavda_input_is_number(const clavda_input_t *input) {
  return input && cpp(input)->isNumber();
}

bool clavda_input_is_text(const clavda_input_t *input) {
  return input && cpp(input)->isText();
}

bool clavda_input_as_bool(const clavda_input_t *input) {
  return input && cpp(input)->asBool();
}

long clavda_input_as_int(const clavda_input_t *input) {
  return input ? cpp(input)->asInt() : 0;
}

double clavda_input_as_double(const clavda_input_t *input) {
  return input ? cpp(input)->asDouble() : 0;
}

const char *clavda_input_as_text(const clavda_input_t *input) {
  return input ? cpp(input)->asText() : "";
}
