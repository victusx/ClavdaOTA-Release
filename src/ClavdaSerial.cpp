#include "ClavdaSerial.h"

bool ClavdaSerialClass::begin(Print *mirror, size_t bufferSize) {
  _mirror = mirror;
  if (_buf) return true;
  if (bufferSize < 256) bufferSize = 256;
  _buf = (uint8_t *)malloc(bufferSize);
  if (!_buf) {
    log_e("[ClavdaOTA] No memory for a %u-byte ClavdaSerial buffer", (unsigned)bufferSize);
    return false;
  }
  _cap = bufferSize;
  return true;
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
  if (_mirror) _mirror->write(buffer, size);
  // Before begin() this is just the mirror, so a sketch can print from the
  // first line of setup() without losing anything on USB.
  if (!_buf) return size;

  // Only the last _cap bytes of a write can survive it.
  const size_t skip = size > _cap ? size - _cap : 0;
  const uint8_t *src = buffer + skip;
  const size_t n = size - skip;

  portENTER_CRITICAL(&_lock);
  const size_t pos = (size_t)((_head + skip) % _cap);
  const size_t first = n < _cap - pos ? n : _cap - pos;
  memcpy(_buf + pos, src, first);
  memcpy(_buf, src + first, n - first);
  _head += size;
  portEXIT_CRITICAL(&_lock);
  return size;
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
    String msg;
    msg.reserve(len);
    msg.concat((const char *)data, len);
    _onString(msg);
  }
}

ClavdaSerialClass ClavdaSerial;
