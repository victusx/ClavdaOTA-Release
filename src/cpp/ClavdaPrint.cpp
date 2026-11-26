#include "ClavdaPrint.h"

#if !CLAVDA_ARDUINO

  #include <stdio.h>
  #include <stdlib.h>

size_t ClavdaPrint::write(const uint8_t *buffer, size_t size) {
  size_t n = 0;
  while (size--) {
    if (!write(*buffer++)) break;
    n++;
  }
  return n;
}

size_t ClavdaPrint::printf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  const size_t n = vprintf(format, args);
  va_end(args);
  return n;
}

size_t ClavdaPrint::vprintf(const char *format, va_list args) {
  if (!format) return 0;
  // Most lines fit on the stack; a longer one is formatted again into the heap.
  char line[128];
  va_list again;
  va_copy(again, args);
  const int len = vsnprintf(line, sizeof(line), format, args);
  size_t written = 0;
  if (len > 0 && (size_t)len < sizeof(line)) {
    written = write((const uint8_t *)line, (size_t)len);
  } else if (len > 0) {
    char *text = (char *)malloc((size_t)len + 1);
    if (text) {
      vsnprintf(text, (size_t)len + 1, format, again);
      written = write((const uint8_t *)text, (size_t)len);
      free(text);
    }
  }
  va_end(again);
  return written;
}

size_t ClavdaPrint::printDigits(unsigned long long n, int base) {
  if (base < 2 || base > 36) base = 10;
  char buf[65];
  char *p = buf + sizeof(buf);
  do {
    const unsigned digit = (unsigned)(n % (unsigned)base);
    *--p = (char)(digit < 10 ? '0' + digit : 'A' + digit - 10);
    n /= (unsigned)base;
  } while (n);
  return write(p, (size_t)(buf + sizeof(buf) - p));
}

// Negative numbers get a sign in base 10 only; in other bases they print as
// the unsigned value of the same width, as Arduino's Print does.
size_t ClavdaPrint::print(long n, int base) {
  if (base == 10 && n < 0) return print('-') + printDigits(0ULL - (unsigned long long)n, 10);
  return base == 10 ? printDigits((unsigned long long)n, 10) : print((unsigned long)n, base);
}

size_t ClavdaPrint::print(unsigned long n, int base) {
  return printDigits(n, base);
}

size_t ClavdaPrint::print(long long n, int base) {
  if (base == 10 && n < 0) return print('-') + printDigits(0ULL - (unsigned long long)n, 10);
  return printDigits((unsigned long long)n, base);
}

size_t ClavdaPrint::print(unsigned long long n, int base) {
  return printDigits(n, base);
}

size_t ClavdaPrint::print(double n, int digits) {
  if (digits < 0) digits = 0;
  if (digits > 17) digits = 17;
  char buf[48];
  const int len = snprintf(buf, sizeof(buf), "%.*f", digits, n);
  if (len <= 0) return 0;
  return write(buf, (size_t)len < sizeof(buf) ? (size_t)len : sizeof(buf) - 1);
}

#endif
