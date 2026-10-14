/**
 * ClavdaOTA for C.
 *
 * The same library as the C++ API - updates, the Serial Monitor and the
 * dashboard - called as plain C functions, for firmware written in C. It is
 * built on the C++ implementation, so the project still compiles that part
 * as C++ (Arduino IDE, PlatformIO and ESP-IDF with the Arduino component all
 * do); the code that includes this header can be pure C.
 *
 *   #define CLAVDA_FIRMWARE_VERSION "1.0.0"
 *   #include <ClavdaOTA.h>          // from a .c file, this header
 *   #include "clavda_device.h"
 *
 *   static clavda_widget_t *temperature;
 *
 *   void app_setup(void) {
 *     clavda_wifi_begin("ssid", "password");
 *     temperature = clavda_widget_create("temperature");
 *     clavda_begin(&CLAVDA_DEVICE);
 *   }
 *
 *   void app_loop(void) {
 *     clavda_loop();
 *     clavda_widget_set_float(temperature, 23.4f);
 *   }
 *
 * Every function that takes a widget or an input does nothing - or returns
 * 0, false or "" - when given NULL, so a widget that failed to allocate does
 * not crash the sketch.
 */

#ifndef CLAVDA_OTA_C_H
#define CLAVDA_OTA_C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../common/clavda_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Updates ---------------------------------------------------------- */

/**
 * Resolve the device identity (NVS first, then the bundle), and start
 * talking to the relay. Returns false when there is no identity at all.
 * Wi-Fi is the sketch's job (see clavda_wifi_begin()); the library waits for it.
 */
bool clavda_begin(const ClavdaBundle *bundle);
/** Call from the main loop, as often as possible. */
void clavda_loop(void);

ClavdaStatus clavda_status(void);
/** Identity in use, from NVS. "" until clavda_begin() succeeds. */
const char *clavda_device_id(void);
const char *clavda_firmware_version(void);
/** The running image carries another device's credentials; NVS kept its own. */
bool clavda_bundle_mismatch(void);

/*
 * Update callbacks. Each gets back the `user` pointer it was registered with;
 * pass NULL for a callback to remove it.
 */

/** Return false to put an update off, e.g. while a motor runs: the console sees it deferred. */
typedef bool (*clavda_update_request_fn)(const ClavdaUpdateRequest *request, void *user);
typedef void (*clavda_update_start_fn)(void *user);
typedef void (*clavda_update_progress_fn)(size_t current, size_t total, void *user);
typedef void (*clavda_update_end_fn)(bool success, void *user);

void clavda_on_update_request(clavda_update_request_fn callback, void *user);
void clavda_on_update_start(clavda_update_start_fn callback, void *user);
void clavda_on_update_progress(clavda_update_progress_fn callback, void *user);
void clavda_on_update_end(clavda_update_end_fn callback, void *user);

/* ---- Wi-Fi ------------------------------------------------------------- */

/**
 * Join a network as a station. The library tracks the connection through the
 * Arduino Wi-Fi driver, so start Wi-Fi with this rather than with esp_wifi_*
 * calls of your own. It reconnects by itself; there is nothing to call again.
 */
void clavda_wifi_begin(const char *ssid, const char *password);
bool clavda_wifi_connected(void);

/* ---- Serial Monitor ---------------------------------------------------- */

/**
 * Start the buffer that console Serial Monitors read from. A `mirror_baud`
 * other than 0 also starts Serial (the USB port) at that rate and sends
 * everything printed there too, so C code needs no Serial.begin() and can
 * log through this alone; 0 leaves Serial alone. `buffer_size` 0 means
 * CLAVDA_SERIAL_BUFFER_SIZE (2 KB). Returns false if the buffer could not be
 * allocated.
 */
bool clavda_serial_begin(unsigned long mirror_baud, size_t buffer_size);
size_t clavda_serial_write(const void *data, size_t len);
size_t clavda_serial_print(const char *text);
/** clavda_serial_print() and a line ending. */
size_t clavda_serial_println(const char *text);
size_t clavda_serial_printf(const char *format, ...) __attribute__((format(printf, 1, 2)));

/** A line typed in a console Serial Monitor, without its line ending, NUL-terminated. */
typedef void (*clavda_serial_message_fn)(const char *line, size_t len, void *user);
void clavda_serial_on_message(clavda_serial_message_fn callback, void *user);

/** Serial Monitors open on this device right now. */
size_t clavda_serial_monitors(void);

/* ---- Dashboard --------------------------------------------------------- */

/** One card or chart on this device's dashboard, named by its ID in the console. */
typedef struct clavda_widget clavda_widget_t;
/** What a dashboard control sent. Valid only inside the change callback. */
typedef struct clavda_input clavda_input_t;

/**
 * A widget with the ID the console gave it. The ID is not copied: pass a
 * string literal, or one that outlives the widget. Create widgets in setup,
 * and use them from the same task as clavda_loop(). NULL if out of memory.
 */
clavda_widget_t *clavda_widget_create(const char *id);
/** Removes it from the dashboard. Its ID string can be released after this. */
void clavda_widget_free(clavda_widget_t *widget);

/*
 * Values. A value goes to every open dashboard only when it changes, so
 * setting it on every loop costs nothing extra.
 */

/** Value, gauge and slider cards. */
void clavda_widget_set_number(clavda_widget_t *widget, double value);
/** As set_number, shown with a float's precision. */
void clavda_widget_set_float(clavda_widget_t *widget, float value);
void clavda_widget_set_int(clavda_widget_t *widget, long value);
/** Toggles. */
void clavda_widget_set_bool(clavda_widget_t *widget, bool on);
/** Text and value cards. Cut to CLAVDA_WIDGET_MAX_TEXT bytes. */
void clavda_widget_set_text(clavda_widget_t *widget, const char *text);
/** A status card's message and colour: CLAVDA_INFO, _SUCCESS, _WARNING, _DANGER. */
void clavda_widget_set_status(clavda_widget_t *widget, const char *text, ClavdaLevel level);

/** One point on a line chart. The console times it, so the board needs no clock. */
void clavda_widget_add_point(clavda_widget_t *widget, float value);
/**
 * A bar chart's bars; `labels` may be NULL. The arrays stay yours and are
 * read when sent, so keep them alive, and call this again after changing them.
 */
void clavda_widget_set_series(clavda_widget_t *widget, const char *const labels[],
                              const float values[], size_t count);

/**
 * Called from clavda_loop() when someone uses this widget's control. By then
 * the widget already holds the new value, and every open dashboard shows it;
 * set something else here to override it. NULL removes the callback.
 */
typedef void (*clavda_widget_change_fn)(clavda_widget_t *widget, const clavda_input_t *input,
                                        void *user);
void clavda_widget_on_change(clavda_widget_t *widget, clavda_widget_change_fn callback,
                             void *user);

const char *clavda_widget_id(const clavda_widget_t *widget);
/** The value as a number: text is parsed, a toggle is 1 or 0. */
double clavda_widget_number(const clavda_widget_t *widget);
bool clavda_widget_is_on(const clavda_widget_t *widget);
const char *clavda_widget_text(const clavda_widget_t *widget);

/* What a control sent: a toggle, slider, button or text box. */
bool clavda_input_is_bool(const clavda_input_t *input);
bool clavda_input_is_number(const clavda_input_t *input);
bool clavda_input_is_text(const clavda_input_t *input);
/** A toggle's state. Numbers count as on when not 0, text when "true", "on" or "1". */
bool clavda_input_as_bool(const clavda_input_t *input);
/** Rounded to the nearest whole number. */
long clavda_input_as_int(const clavda_input_t *input);
double clavda_input_as_double(const clavda_input_t *input);
/** The text as typed, or a number or boolean written out. Copy it to keep it. */
const char *clavda_input_as_text(const clavda_input_t *input);

#ifdef __cplusplus
}
#endif

#endif
