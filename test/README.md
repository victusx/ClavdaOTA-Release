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
