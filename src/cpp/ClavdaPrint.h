#ifndef ClavdaPrint_h
#define ClavdaPrint_h

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include "../common/clavda_types.h"

#if CLAVDA_ARDUINO

  #include <Print.h>
/** On the Arduino core, Arduino's own Print: F(), String, Printable and the rest. */
using ClavdaPrint = Print;

#else

/**
 * The Arduino Print API without the Arduino core: write(), print(),
 * println() and printf(), so code that prints to ClavdaSerial builds under
 * ESP-IDF unchanged. Integers take a base (2 to 36, 10 by default) and
 * floating point a number of decimals (2 by default), as on Arduino.
 */
class ClavdaPrint {
  public:
    virtual ~ClavdaPrint() = default;

    virtual size_t write(uint8_t c) = 0;
    virtual size_t write(const uint8_t *buffer, size_t size);
    size_t write(const char *text) { return text ? write((const uint8_t *)text, strlen(text)) : 0; }
    size_t write(const char *buffer, size_t size) { return write((const uint8_t *)buffer, size); }

    size_t printf(const char *format, ...) __attribute__((format(printf, 2, 3)));
    size_t vprintf(const char *format, va_list args);

    size_t print(const char *text) { return write(text); }
    size_t print(const std::string &text) { return write(text.data(), text.size()); }
    size_t print(char c) { return write((uint8_t)c); }
    size_t print(int n, int base = 10) { return print((long)n, base); }
    size_t print(unsigned int n, int base = 10) { return print((unsigned long)n, base); }
    size_t print(long n, int base = 10);
    size_t print(unsigned long n, int base = 10);
    size_t print(long long n, int base = 10);
    size_t print(unsigned long long n, int base = 10);
    size_t print(double n, int digits = 2);

    size_t println() { return write("\r\n", 2); }
    template <typename T>
    size_t println(const T &value) {
      const size_t n = print(value);
      return n + println();
    }
    template <typename T>
    size_t println(const T &value, int format) {
      const size_t n = print(value, format);
      return n + println();
    }

  private:
    size_t printDigits(unsigned long long n, int base);
};

#endif

#endif
