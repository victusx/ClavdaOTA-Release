<p align="center"><i>Over-the-air firmware updates for the ESP32 family - from anywhere.</i></p>

ClavdaOTA 2 connects an ESP32 to the Clavda relay, so it can be updated from the Clavda console wherever it is: behind NAT, carrier-grade CGNAT or a firewall, with no open port and nothing listening on the device. Upload a `.bin` in the console, press **Update now**, and watch it land.

> [!IMPORTANT]
> **2.0 is cloud-only.** The local `/update` web portal, `begin(&server)`, `setAuth()` and the async-webserver option are gone. Sketches written for 1.x need the changes in [Quick start](#quick-start).

## Features

- 🌍 Update a device anywhere - it dials out over TLS, so no port forwarding and no VPN
- 🔐 Firmware downloads straight from storage over a short-lived link, checked against its SHA-256 before anything is installed
- 🪪 One identity per board, kept in NVS: the same fleet image can go to every device
- 📊 Live progress in the console, and success only once the device is back online running the new version
- 🛟 Refuses images for another chip, images too large for the OTA slot, and images that could not reconnect
- ⏸️ `onUpdateRequest()` lets your application put an update off while it is busy
- 🖥️ A remote Serial Monitor: print to `ClavdaSerial` and watch it live in the console, end-to-end encrypted
- 📈 A live dashboard: cards, gauges, charts and controls built in the console, fed by `ClavdaWidget`, end-to-end encrypted
- 🧩 A C API next to the C++ one, for firmware written in C - see [From C](#from-c)

## Supported MCUs

| Platform | Notes |
| --- | --- |
| **ESP32** | Including S2, S3, C3 and C6 variants. Built and tested against Arduino-ESP32 3.3. |

ESP8266 and RP2040/RP2350 are not supported by 2.x.

## Installation

### Arduino IDE

1. Download the library as a ZIP: **Code → Download ZIP** on [this page](https://github.com/victusx/ClavdaOTA-Release).
2. `Sketch > Include Library > Add .ZIP Library…` and choose the file.
3. `Tools > Manage Libraries…` and install the two libraries it depends on: **WebSockets** by Markus Sattler and **ArduinoJson** by Benoit Blanchon. A ZIP install does not fetch them for you.

### PlatformIO

```ini
lib_deps =
  https://github.com/victusx/ClavdaOTA-Release.git
```

The WebSockets and ArduinoJson dependencies are pulled in from `library.json`.

## Quick start

1. In the Clavda console: **Devices → Add Device**, installation platform **ESP32**. It counts toward your device quota like any other device.
2. **Manage → Get Device File → Download clavda_device.h**, and put it next to your sketch (Arduino IDE) or in `include/` (PlatformIO).
3. Include the library and the file, and call `begin()` and `loop()`:

```cpp
#include <WiFi.h>

#define CLAVDA_FIRMWARE_VERSION "1.0.0"   // bump for every build you upload
#include <ClavdaOTA.h>
#include "clavda_device.h"

void setup() {
  WiFi.begin("ssid", "password");
  ClavdaOTA.begin(CLAVDA_DEVICE);
}

void loop() {
  ClavdaOTA.loop();
}
```

4. Flash it over USB **once**. The device shows as online in the console.
5. From then on: build a new version, open the device's **Connect** sheet, upload the `.bin` and press **Update now**.

The full sketch is in [`examples/cpp/CloudDemo`](examples/cpp/CloudDemo/CloudDemo.ino), and in C in [`examples/c/CloudDemo`](examples/c/CloudDemo/cloud_demo.c).

> [!WARNING]
> `clavda_device.h` contains the device's token and job key. Keep it out of git - the library's `.gitignore` already lists it - and do not share it. Every download is recorded as a device notification in the console.

### The firmware version

`CLAVDA_FIRMWARE_VERSION` is compiled into the image and read by the console when you upload it, so you never type it in. The console counts an update as done only when the device reconnects reporting that version, so **give every build you upload a new one**. Letters, digits and `. _ + -`, up to 31 characters.

### Partition scheme

The update is written to the second OTA slot, so the partition scheme needs two. The Arduino default (1.25 MB per app) is tight: the CloudDemo example alone is 1.1-1.25 MB depending on the chip, so most projects want **Tools → Partition Scheme → Minimal SPIFFS (1.9MB APP with OTA)** or larger.

## Serial Monitor

Print to `ClavdaSerial` the way you would print to `Serial`, and the output shows up live in the console: open the device's **Connect** sheet and press **Serial Monitor**. It travels over the same outbound connection as updates, so it works wherever the device is. Lines typed in the console arrive in `onMessage()`.

```cpp
void setup() {
  Serial.begin(115200);
  ClavdaSerial.begin(&Serial);   // also mirror everything to USB
  ClavdaSerial.onMessage([](const String &msg) {
    ClavdaSerial.printf("Received: %s\n", msg.c_str());
  });

  WiFi.begin("ssid", "password");
  ClavdaOTA.begin(CLAVDA_DEVICE);
}

void loop() {
  ClavdaOTA.loop();
  ClavdaSerial.printf("Free heap: %u\n", ESP.getFreeHeap());
  delay(1000);
}
```

- The last 2 KB of output is kept on the device, so a monitor that opens late - or after a reboot - starts with the recent output, boot messages included. `ClavdaSerial.begin(&Serial, 8192)` keeps more; it is the only RAM `ClavdaSerial` holds while nobody is watching.
- Each monitor gets its own key from the console, so the relay carries the output without being able to read it. Up to 3 monitors can watch one device at once.
- Typing into the device needs permission to edit devices; everyone who can see the device can watch.
- `ClavdaSerial` is safe to print to from any task. Output printed while the device is offline is sent when it reconnects, as far as the buffer reaches; the console marks anything it missed.

Coming from **WebSerial**: `begin()` takes an optional mirror instead of a web server, and `print`, `printf`, `onMessage()` work as before. There is no web page on the device and no port to open. The full sketch is in [`examples/cpp/SerialMonitor`](examples/cpp/SerialMonitor/SerialMonitor.ino), and in C in [`examples/c/SerialMonitor`](examples/c/SerialMonitor/serial_monitor.c).

## Dashboard

Build a dashboard for the device in the console - **Connect → Dashboard → Edit** - from value cards, gauges, status cards, line and bar charts, toggles, sliders, buttons and text inputs. Every widget there has an ID; a `ClavdaWidget` with the same ID feeds it:

```cpp
ClavdaWidget temperature("temperature");  // a Value or Gauge card
ClavdaWidget history("temp_history");     // a Line chart
ClavdaWidget relay("relay");              // a Toggle

void setup() {
  relay.onChange([](const ClavdaInput &in) { digitalWrite(RELAY_PIN, in.asBool()); });
  // Wi-Fi, then:
  ClavdaOTA.begin(CLAVDA_DEVICE);
}

void loop() {
  ClavdaOTA.loop();
  float celsius = readTemperature();
  temperature.set(celsius);  // sent only when it changes
  history.add(celsius);      // a chart point; the console times it, so no RTC is needed
}
```

- The layout lives in the console, so it changes without reflashing. Each widget's settings show the line to paste, and **Firmware code** gives an outline for the whole dashboard. IDs the sketch sends that no widget shows yet are listed in edit mode, ready to add.
- `set()` takes a number, a `bool`, or text; `set("Door open", CLAVDA_WARNING)` colours a status card. `setSeries(labels, values, n)` fills a bar chart. A control's new value is already the widget's when `onChange()` runs, and every open dashboard shows it.
- Each open dashboard gets its own key, like a Serial Monitor, and the device only takes control input from people with permission to edit devices. Up to 3 dashboards can be open on one device.
- Values are sent at most every 100 ms per dashboard, and only what changed. A widget costs about 70 bytes; line charts share a ring of the last 64 points (`CLAVDA_DASH_POINTS`), allocated on the first `add()`. Use widgets from `setup()` and `loop()`, not from other tasks.

The full sketch is in [`examples/cpp/Dashboard`](examples/cpp/Dashboard/Dashboard.ino), and in C in [`examples/c/Dashboard`](examples/c/Dashboard/dashboard.c).

## From C

Firmware written in C uses the same library through its C API: plain functions, opaque handles for widgets, and function pointers with a `user` pointer for callbacks. Include `<ClavdaOTA.h>` from a `.c` file and you get the C API; from C++ you get the classes above, and can call the C functions too. The console's `clavda_device.h` works from either.

```c
#define CLAVDA_FIRMWARE_VERSION "1.0.0"
#include <ClavdaOTA.h>
#include "clavda_device.h"

static clavda_widget_t *temperature;
static clavda_widget_t *relay;

static void on_relay(clavda_widget_t *w, const clavda_input_t *in, void *user) {
  digitalWrite(RELAY_PIN, clavda_input_as_bool(in));
}

void app_setup(void) {
  clavda_serial_begin(115200, 0);                // also starts Serial and prints there; 2 KB buffer
  temperature = clavda_widget_create("temperature");
  relay = clavda_widget_create("relay");
  clavda_widget_on_change(relay, on_relay, NULL);

  clavda_wifi_begin("ssid", "password");
  clavda_begin(&CLAVDA_DEVICE);
}

void app_loop(void) {
  clavda_loop();
  clavda_widget_set_float(temperature, read_temperature());
}
```

- The C API is a thin layer over the C++ implementation, so both behave the same and the library itself still compiles as C++. The Arduino IDE, PlatformIO, and ESP-IDF with the Arduino component all do that on their own; only your code needs to be C.
- Start Wi-Fi with `clavda_wifi_begin()`: the library follows the connection through the Arduino Wi-Fi driver, which `esp_wifi_*` calls of your own would bypass.
- A widget's ID is not copied - pass a string literal. Every function that takes a widget or an input accepts `NULL` and does nothing, so a widget that failed to allocate cannot crash the sketch.
- Print with `clavda_serial_printf()` and friends: `clavda_serial_begin(115200, 0)` starts Serial itself, which C code cannot, and everything printed goes to USB and to the console's Serial Monitor.
- In the Arduino IDE, `setup()` and `loop()` are called as C++, so a sketch in C keeps a short `.ino` that calls into the `.c` file. Every example comes in both languages: [`examples/c/`](examples/c) next to [`examples/cpp/`](examples/cpp).

| C++ | C |
| --- | --- |
| `ClavdaOTA.begin(CLAVDA_DEVICE)` / `loop()` | `clavda_begin(&CLAVDA_DEVICE)` / `clavda_loop()` |
| `ClavdaOTA.status()` / `deviceId()` / `firmwareVersion()` / `bundleMismatch()` | `clavda_status()` / `clavda_device_id()` / `clavda_firmware_version()` / `clavda_bundle_mismatch()` |
| `ClavdaOTA.onUpdateRequest()` / `onStart()` / `onProgress()` / `onEnd()` | `clavda_on_update_request()` / `_start()` / `_progress()` / `_end()`, each `(callback, user)` |
| `WiFi.begin(ssid, password)` | `clavda_wifi_begin(ssid, password)`, `clavda_wifi_connected()` |
| `Serial.begin(115200)`, `ClavdaSerial.begin(&Serial, size)` | `clavda_serial_begin(115200, size)` - starts Serial and mirrors to it; `0` baud for no mirror, `0` size for the default |
| `ClavdaSerial.print()` / `println()` / `printf()` / `write()` | `clavda_serial_print()` / `_println()` / `_printf()` / `_write()` |
| `ClavdaSerial.onMessage(cb)` / `monitors()` | `clavda_serial_on_message(cb, user)` - the line arrives NUL-terminated / `clavda_serial_monitors()` |
| `ClavdaWidget w("id")` | `clavda_widget_t *w = clavda_widget_create("id")`, `clavda_widget_free(w)` |
| `w.set(number)` / `set(bool)` / `set(text)` / `set(text, level)` | `clavda_widget_set_number()` / `_float()` / `_int()` / `_bool()` / `_text()` / `_status()` |
| `w.add(value)` / `w.setSeries(labels, values, n)` | `clavda_widget_add_point()` / `clavda_widget_set_series()` |
| `w.onChange(cb)` | `clavda_widget_on_change(w, cb, user)` |
| `in.asBool()` / `asInt()` / `asDouble()` / `asText()` | `clavda_input_as_bool(in)` / `_as_int()` / `_as_double()` / `_as_text()` |

## Memory

On a connected ESP32, nearly all the RAM that goes when ClavdaOTA starts is the TLS connection to the relay, and that is sized by the ESP32 core rather than by the library:

| What | RAM |
| --- | --- |
| The TLS connection to the relay | Roughly 35-40 KB of heap while connected. Arduino-ESP32 3.x builds mbedTLS with a fixed 16 KB receive buffer and a 16 KB send buffer per connection. |
| ClavdaOTA itself | About 5.5 KB of static RAM and in-RAM code on an ESP32-S3 (with its WebSockets and JSON dependencies) and 1-2 KB of heap. |
| `ClavdaSerial`, if you start it | The buffer you give `begin()` - 2 KB by default - plus about 1.5 KB only while a monitor is open. |
| An update in progress | A second TLS connection, to the firmware download, until the image is written. This is what the console's "lowest" free heap reflects. |

Wi-Fi itself typically takes 40-60 KB of heap as well, in any sketch that connects. The [`CloudDemo`](examples/cpp/CloudDemo/CloudDemo.ino) example prints the free heap at boot, once Wi-Fi is up, and once the device reaches Clavda; the differences are what each step costs on your board.

Compared with a sketch that never uses Wi-Fi, such as Blink, part of the difference is there before `setup()` runs: on an ESP32-S3 with Arduino-ESP32 3.3, just linking Wi-Fi and TLS in costs about 29 KB of RAM (static data, plus the Wi-Fi code the core keeps in internal RAM).

### Boards with PSRAM

If the board has PSRAM (the R in module names like N8R2 or N16R8), turn it on: **Tools → PSRAM** in the Arduino IDE, OPI PSRAM for R8 modules and QSPI PSRAM for R2. With PSRAM on:

- the core puts Wi-Fi and lwIP buffers there;
- `ClavdaOTA.begin()` points mbedTLS at it, so the relay connection and an update's download connection stop using internal RAM. This applies to every TLS connection in the sketch;
- `ClavdaSerial`'s buffer goes there too.

`ESP.getFreeHeap()` counts internal RAM only; `ESP.getFreePsram()` shows what is left of the PSRAM.

### Smaller TLS buffers (PlatformIO)

The send side only ever carries small messages, so its buffer can shrink to 4 KB, which saves about 12 KB per connection. The receive buffer has to stay at 16 KB, because the relay and the firmware download send full-size TLS records. The Arduino IDE cannot change these settings, but PlatformIO with pioarduino can rebuild the core with them:

```ini
[env:esp32]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/53.03.11/platform-espressif32.zip
board = esp32dev
framework = arduino
custom_sdkconfig =
  CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN=y
  CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384
  CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=4096
```

The first build with `custom_sdkconfig` recompiles the Arduino core libraries, which takes several minutes. The setting applies to every TLS connection in the sketch.

## Per-device and fleet images

Everything in `clavda_device.h` apart from the relay address and certificate roots is one device's identity. What happens to it:

- **On first boot** the library copies it into NVS. From then on NVS wins: a later image can rotate the same device's credentials, but never replace them with another device's.
- **An image built with the file** (a *per-device image*) can only be installed on that device - the console enforces it. Use it for the first USB flash.
- **An image built with `CLAVDA_FLEET_BUILD` defined** (a *fleet image*) leaves the credentials out, so one `.bin` can go to every device. Each board keeps its own identity from NVS.

```ini
; platformio.ini
build_flags = -D CLAVDA_FLEET_BUILD
```

To give a board a different identity, erase its flash (*Tools → Erase All Flash Before Sketch Upload*, or `pio run -t erase`) and flash an image built with the new device's file.

## How an update works

```
Console ── sealed job ──▶ Clavda relay ──▶ ESP32 ── HTTPS GET ──▶ storage (presigned, 15 min)
                                             │
                                             ├─ SHA-256 matches?  → install, reboot
                                             └─ reconnects running the new version → done
```

The job travels encrypted with the device's own key, so the relay never sees the download link. The image goes into the inactive OTA slot and is only committed once its SHA-256 matches; any failure leaves the running firmware untouched.

## API

| Method | Purpose |
| --- | --- |
| `begin(CLAVDA_DEVICE)` | Load or seed the identity and start talking to the relay. Returns `false` when the board has no identity. |
| `loop()` | Call from `loop()`. |
| `onUpdateRequest(cb)` | `bool cb(const ClavdaUpdateRequest &)` - return `false` to put an update off. |
| `onStart(cb)` / `onProgress(cb)` / `onEnd(cb)` | Update lifecycle callbacks. |
| `status()` | `CLAVDA_UNPROVISIONED`, `CLAVDA_CONNECTING`, `CLAVDA_ONLINE`, `CLAVDA_UPDATING` or `CLAVDA_REJECTED`. |
| `deviceId()` / `firmwareVersion()` / `bundleMismatch()` | What the board is running as. |
| `ClavdaSerial.begin(mirror, bufferSize)` | Start the Serial Monitor buffer (2 KB by default). `mirror`, e.g. `&Serial`, also gets everything printed. |
| `ClavdaSerial.print()` / `println()` / `printf()` / `write()` | A `Print`, like `Serial`. |
| `ClavdaSerial.onMessage(cb)` | `void cb(const String &msg)` or `void cb(uint8_t *data, size_t len)` - a line typed in the console. |
| `ClavdaSerial.monitors()` | How many Serial Monitors are open on this device. |
| `ClavdaWidget w("id")` | A dashboard widget, named by its ID in the console. Declare it at global scope. |
| `w.set(value)` / `w.set(text, level)` | Its value: a number, `bool` or text; a status card's message and `CLAVDA_INFO` / `_SUCCESS` / `_WARNING` / `_DANGER`. |
| `w.add(value)` / `w.setSeries(labels, values, n)` | A line-chart point; a bar chart's bars. |
| `w.onChange(cb)` | `void cb(const ClavdaInput &in)` - a toggle, slider, button or text input was used. `in.asBool()`, `asInt()`, `asFloat()`, `asText()`. |

Diagnostics go through the ESP32 log: set **Tools → Core Debug Level → Info** to see them.

## Library layout

| Path | What is there |
| --- | --- |
| `src/ClavdaOTA.h` | The header to include, from C or C++. |
| `src/clavda_ota.h` | The C API on its own. |
| `src/common/` | Types and macros both APIs share, in plain C: the bundle, the firmware descriptor, `ClavdaStatus`, `ClavdaLevel`. |
| `src/cpp/` | The implementation, and the C++ API. |
| `src/c/` | The C API, on top of `src/cpp/`. |
| `examples/cpp/` | `CloudDemo`, `SerialMonitor` and `Dashboard`, in C++. |
| `examples/c/` | The same three in C: a short `.ino` that calls into the sketch's `.c` file. |
| `test/` | On-device tests of both APIs; see [`test/README.md`](test/README.md). Kept out of `src/`, which is compiled into every sketch. |

## Contributing

Every contribution is highly appreciated. Spotted a bug? Open an issue or a pull request so it can be fixed for everyone.

**Feature requests:** open an issue and I'll look at adding it in a future release.

Contributors are asked to sign the [CLA](CLA.md), and participation is governed by the [Code of Conduct](CODE_OF_CONDUCT.md).

<br/>

## License

ClavdaOTA is licensed under the **GNU Affero General Public License v3.0 (AGPL-3.0)**.

ClavdaOTA is based on [ElegantOTA](https://github.com/ayushsharma82/ElegantOTA) by Ayush Sharma, also licensed under AGPL-3.0.
