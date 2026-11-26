# Tests

On-device checks of the library's two APIs. They are sketches, so they run
from the Arduino IDE as well as from the command line, and need no Wi-Fi and
no provisioned device.

| Sketch | Checks |
| --- | --- |
| `CApiTest/` | The C API, from `c_api_test.c`, compiled as C: the bundle a `clavda_device.h` makes, widgets, NULL handling, the Serial Monitor buffer. |
| `CppApiTest/` | The C++ API: `ClavdaWidget`, `ClavdaSerial`, `ClavdaOTA`, and the C API called from C++. |

They live here rather than under `src/` because the Arduino IDE and
PlatformIO compile everything under `src/` into every sketch that uses the
library.

## Running

Open a sketch in the Arduino IDE, flash it, and open the serial monitor at
115200 baud. The last line is `ALL PASSED`, or `FAILED` after the checks
that failed.

With `arduino-cli`, from the repository root (`--library .` builds this copy
of the library rather than an installed one):

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 --library . test/CApiTest
arduino-cli upload  --fqbn esp32:esp32:esp32 -p COM5 test/CApiTest
arduino-cli monitor -p COM5 -c baudrate=115200
```

Compiling alone - every example and both tests - is a quick check that the
C header still builds as C and the C++ headers as C++.

## ESP-IDF

The tests are Arduino sketches. Under ESP-IDF, building
the projects in [`examples/idf/`](../examples/idf) is the check that the
library compiles as a component without the Arduino core:

```sh
cd examples/idf/CloudDemo
idf.py set-target esp32c3
idf.py build
```

On Windows, keep the build folder's path short (`idf.py -B C:\b\clavda build`):
mbedTLS's object paths are long enough to pass the 260-character limit when
the project sits deep in a user folder.

The component manager writes `managed_components/` and `dependencies.lock`
into the example's folder, and `idf.py` an `sdkconfig` next to them unless
`-D SDKCONFIG=` points elsewhere. All are in `.gitignore`; delete them when
you are done.
