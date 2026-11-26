#include "ClavdaSerial.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "ClavdaPlatform.h"

/** What logged before captureLogs() took over; the console still gets everything. */
static vprintf_like_t previousLog = nullptr;

#if CLAVDA_ARDUINO
bool ClavdaSerialClass::begin(Print *mirror, size_t bufferSize) {
#else
bool ClavdaSerialClass::begin(bool mirror, size_t bufferSize) {
#endif
  _mirror = mirror;
  if (_buf) return true;
  if (bufferSize < 256) bufferSize = 256;
  // PSRAM when the board has it: malloc() alone only puts blocks of 4 KB and
  // up there, so the default 2 KB would stay in internal RAM.
  uint8_t *buf = (uint8_t *)heap_caps_malloc_prefer(bufferSize, 2,
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!buf) {
    CLAVDA_LOGE("No memory for a %u-byte ClavdaSerial buffer", (unsigned)bufferSize);
    return false;
  }
  portENTER_CRITICAL(&_lock);
  _cap = bufferSize;
  _buf = buf;
  portEXIT_CRITICAL(&_lock);
  return true;
}

int ClavdaSerialClass::logHook(const char *format, va_list args) {
  // Formatted once for the buffer; the console's own vprintf formats it again.
  char line[160];
  va_list copy;
  int len = 0;
  if (ClavdaSerial._buf) {
    va_copy(copy, args);
    len = vsnprintf(line, sizeof(line), format, copy);
    va_end(copy);
  }
  if (len > 0) {
    char *text = line;
    if ((size_t)len >= sizeof(line)) {
      text = (char *)malloc((size_t)len + 1);
      if (text) {
        va_copy(copy, args);
        vsnprintf(text, (size_t)len + 1, format, copy);
        va_end(copy);
      }
    }
    if (text) {
      // Colour codes mean nothing in a browser: drop ESC [ ... m.
      size_t out = 0;
      for (int i = 0; i < len; i++) {
        if (text[i] == '\033' && i + 1 < len && text[i + 1] == '[') {
          i += 2;
          while (i < len && text[i] != 'm') i++;
          continue;
        }
        text[out++] = text[i];
      }
      ClavdaSerial.record((const uint8_t *)text, out);
      if (text != line) free(text);
    }
  }
  return previousLog ? previousLog(format, args) : ::vprintf(format, args);
}

void ClavdaSerialClass::captureLogs(bool on) {
  if (on && !previousLog) {
    previousLog = esp_log_set_vprintf(logHook);
  } else if (!on && previousLog) {
    esp_log_set_vprintf(previousLog);
    previousLog = nullptr;
  }
}

void ClavdaSerialClass::onMessage(MessageHandler handler) {
  _onMessage = handler;
  _onString = nullptr;
}

void ClavdaSerialClass::onMessage(StringMessageHandler handler) {
  _onString = handler;
  _onMessage = nullptr;
}

size_t ClavdaSerialClass::write(uint8_t c) {
  return write(&c, 1);
}

size_t ClavdaSerialClass::write(const uint8_t *buffer, size_t size) {
  if (!buffer || !size) return 0;
#if CLAVDA_ARDUINO
  if (_mirror) _mirror->write(buffer, size);
#else
  if (_mirror) {
    fwrite(buffer, 1, size, stdout);
    fflush(stdout);
  }
#endif
  // Before begin() this is just the mirror, so a sketch can print from the
  // first line of setup() without losing anything on USB.
  record(buffer, size);
  return size;
}

void ClavdaSerialClass::record(const uint8_t *buffer, size_t size) {
  if (!_buf || !size) return;
  portENTER_CRITICAL(&_lock);
  // Only the last _cap bytes of a write can survive it.
  const size_t skip = size > _cap ? size - _cap : 0;
  const uint8_t *src = buffer + skip;
  const size_t n = size - skip;
  const size_t pos = (size_t)((_head + skip) % _cap);
  const size_t first = n < _cap - pos ? n : _cap - pos;
  memcpy(_buf + pos, src, first);
  memcpy(_buf, src + first, n - first);
  _head += size;
  portEXIT_CRITICAL(&_lock);
}

void ClavdaSerialClass::window(uint64_t &oldest, uint64_t &head) {
  portENTER_CRITICAL(&_lock);
  head = _head;
  oldest = _head > _cap ? _head - _cap : 0;
  portEXIT_CRITICAL(&_lock);
}

size_t ClavdaSerialClass::read(uint64_t &from, uint8_t *out, size_t cap) {
  if (!_buf) return 0;
  portENTER_CRITICAL(&_lock);
  const uint64_t oldest = _head > _cap ? _head - _cap : 0;
  if (from < oldest) from = oldest;
  if (from > _head) from = _head;
  size_t n = (size_t)(_head - from);
  if (n > cap) n = cap;
  const size_t pos = (size_t)(from % _cap);
  const size_t first = n < _cap - pos ? n : _cap - pos;
  memcpy(out, _buf + pos, first);
  memcpy(out + first, _buf, n - first);
  portEXIT_CRITICAL(&_lock);
  return n;
}

void ClavdaSerialClass::deliver(uint8_t *data, size_t len) {
  if (_onMessage) {
    _onMessage(data, len);
  } else if (_onString) {
#if CLAVDA_ARDUINO
    String msg;
    msg.reserve(len);
    msg.concat((const char *)data, len);
#else
    const std::string msg((const char *)data, len);
#endif
    _onString(msg);
  }
}

ClavdaSerialClass ClavdaSerial;
