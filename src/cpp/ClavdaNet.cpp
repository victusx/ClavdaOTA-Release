#include "ClavdaNet.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <memory>
#include "esp_idf_version.h"
#include "esp_tls_errors.h"
#include "esp_transport_ssl.h"
#include "esp_transport_tcp.h"
#if ESP_IDF_VERSION_MAJOR >= 5
  #include "esp_random.h"
#else
  #include "esp_system.h"
#endif

#include "ClavdaCrypto.h"
#include "ClavdaPlatform.h"

namespace clavda {

// Writes are small and the socket's send buffer usually takes them at once;
// this only bounds a link that has stopped draining.
static const int WRITE_TIMEOUT_MS = 10000;
// Longest response head accepted: status line and headers.
static const size_t MAX_HEAD = 4096;

// ---- Url ----

std::string Url::hostHeader() const {
  const uint16_t standard = tls ? 443 : 80;
  return port == standard ? host : format("%s:%u", host.c_str(), (unsigned)port);
}

bool parseUrl(const std::string &url, Url &out) {
  out = Url();
  size_t at;
  if (startsWith(url, "https://")) {
    out.tls = true;
    at = 8;
  } else if (startsWith(url, "wss://")) {
    out.tls = true;
    at = 6;
  } else if (startsWith(url, "http://")) {
    at = 7;
  } else if (startsWith(url, "ws://")) {
    at = 5;
  } else {
    return false;
  }

  const size_t slash = url.find_first_of("/?#", at);
  const std::string authority = url.substr(at, slash == std::string::npos ? std::string::npos
                                                                          : slash - at);
  out.path = slash == std::string::npos ? "/" : url.substr(slash);
  if (out.path[0] != '/') out.path.insert(0, "/");
  // A fragment never goes on the wire.
  const size_t hash = out.path.find('#');
  if (hash != std::string::npos) out.path.resize(hash);

  // No credentials in the authority, ever; and no IPv6 literals.
  if (authority.find('@') != std::string::npos || authority.find('[') != std::string::npos) {
    return false;
  }
  const size_t colon = authority.find(':');
  out.host = authority.substr(0, colon);
  out.port = out.tls ? 443 : 80;
  if (colon != std::string::npos) {
    const long port = strtol(authority.c_str() + colon + 1, nullptr, 10);
    if (port <= 0 || port > 65535) return false;
    out.port = (uint16_t)port;
  }
  return !out.host.empty();
}

// ---- Connection ----

bool Connection::open(const Url &url, const char *caRoots, int timeoutMs, std::string &error) {
  close();
  if (url.tls) {
    if (!caRoots || !caRoots[0]) {
      error = "no CA roots to check the certificate against";
      return false;
    }
    _t = esp_transport_ssl_init();
    // The length without the NUL: the transport adds it, as PEM parsing needs.
    if (_t) esp_transport_ssl_set_cert_data(_t, caRoots, (int)strlen(caRoots));
  } else {
    _t = esp_transport_tcp_init();
  }
  if (!_t) {
    error = "out of memory";
    return false;
  }

  if (esp_transport_connect(_t, url.host.c_str(), url.port, timeoutMs) < 0) {
    const int err = esp_transport_get_errno(_t);
    error = format("could not connect to %s:%u%s", url.host.c_str(), (unsigned)url.port,
                   url.tls ? " over TLS" : "");
    if (err) error += format(" (%s)", strerror(err));
    close();
    return false;
  }
  return true;
}

void Connection::close() {
  if (!_t) return;
  esp_transport_close(_t);
  esp_transport_destroy(_t);
  _t = nullptr;
}

/** A TLS read or write that only has to be tried again. */
static bool tryAgain(int rc) {
#ifdef ESP_TLS_ERR_SSL_WANT_READ
  if (rc == ESP_TLS_ERR_SSL_WANT_READ) return true;
#endif
#ifdef ESP_TLS_ERR_SSL_WANT_WRITE
  if (rc == ESP_TLS_ERR_SSL_WANT_WRITE) return true;
#endif
#ifdef ESP_TLS_ERR_SSL_TIMEOUT
  if (rc == ESP_TLS_ERR_SSL_TIMEOUT) return true;
#endif
  return false;
}

int Connection::read(uint8_t *buf, size_t cap) {
  if (!_t) return -1;
  const int n = esp_transport_read(_t, (char *)buf, (int)cap, 0);
  if (n > 0) return n;
  // 0 is "nothing within the timeout", and the timeout is 0.
  if (n == 0 || tryAgain(n)) return 0;
  return -1;
}

bool Connection::write(const void *data, size_t len, int timeoutMs) {
  if (!_t) return false;
  const char *p = (const char *)data;
  const uint32_t start = nowMs();
  while (len) {
    const int elapsed = (int)(nowMs() - start);
    if (elapsed >= timeoutMs) return false;
    const int n = esp_transport_write(_t, p, (int)len, timeoutMs - elapsed);
    if (n > 0) {
      p += n;
      len -= (size_t)n;
    } else if (n < 0 && !tryAgain(n)) {
      return false;
    }
  }
  return true;
}

bool Connection::readHead(int timeoutMs, std::string &head, std::string &rest,
                          std::string &error) {
  head.clear();
  rest.clear();
  if (!_t) {
    error = "not connected";
    return false;
  }
  char buf[256];
  const uint32_t start = nowMs();
  for (;;) {
    const size_t end = head.find("\r\n\r\n");
    if (end != std::string::npos) {
      rest = head.substr(end + 4);
      head.resize(end + 2);
      return true;
    }
    if (head.size() > MAX_HEAD) {
      error = "the response headers are too long";
      return false;
    }
    const int elapsed = (int)(nowMs() - start);
    if (elapsed >= timeoutMs) {
      error = "no response in time";
      return false;
    }
    const int n = esp_transport_read(_t, buf, sizeof(buf), timeoutMs - elapsed);
    if (n > 0) {
      head.append(buf, (size_t)n);
    } else if (n < 0 && !tryAgain(n)) {
      error = "the connection closed before a response";
      return false;
    }
  }
}

int httpStatus(const std::string &head) {
  // "HTTP/1.1 200 OK"
  if (!startsWith(head, "HTTP/")) return 0;
  const size_t space = head.find(' ');
  if (space == std::string::npos) return 0;
  return atoi(head.c_str() + space + 1);
}

std::string httpHeader(const std::string &head, const char *name) {
  const size_t nameLen = strlen(name);
  size_t line = head.find("\r\n");
  while (line != std::string::npos) {
    line += 2;
    const size_t end = head.find("\r\n", line);
    if (end == std::string::npos) break;
    const size_t colon = head.find(':', line);
    if (colon != std::string::npos && colon < end && colon - line == nameLen &&
        strncasecmp(head.c_str() + line, name, nameLen) == 0) {
      size_t from = colon + 1;
      size_t to = end;
      while (from < to && (head[from] == ' ' || head[from] == '\t')) from++;
      while (to > from && (head[to - 1] == ' ' || head[to - 1] == '\t')) to--;
      return head.substr(from, to - from);
    }
    line = end;
  }
  return std::string();
}

// ---- WebSocket ----

enum : uint8_t {
  OP_CONTINUATION = 0x0,
  OP_TEXT = 0x1,
  OP_BINARY = 0x2,
  OP_CLOSE = 0x8,
  OP_PING = 0x9,
  OP_PONG = 0xA,
};

bool WebSocket::connect(const Url &url, const char *caRoots, int timeoutMs, std::string &error) {
  close();
  _failed = false;
  _rx.clear();
  _message.clear();
  _messageOpcode = 0;
  if (!_conn.open(url, caRoots, timeoutMs, error)) return false;

  uint8_t nonce[16];
  esp_fill_random(nonce, sizeof(nonce));
  char key[32];
  size_t keyLen = 0;
  base64Encode(nonce, sizeof(nonce), key, sizeof(key), keyLen);

  const std::string request = format("GET %s HTTP/1.1\r\n"
                                     "Host: %s\r\n"
                                     "Upgrade: websocket\r\n"
                                     "Connection: Upgrade\r\n"
                                     "Sec-WebSocket-Key: %s\r\n"
                                     "Sec-WebSocket-Version: 13\r\n"
                                     "User-Agent: ClavdaOTA/" CLAVDA_LIB_VERSION "\r\n"
                                     "\r\n",
                                     url.path.c_str(), url.hostHeader().c_str(), key);
  std::string head;
  if (!_conn.write(request.data(), request.size(), timeoutMs) ||
      !_conn.readHead(timeoutMs, head, _rx, error)) {
    if (error.empty()) error = "the upgrade request could not be sent";
    _conn.close();
    return false;
  }
  const int status = httpStatus(head);
  if (status != 101) {
    error = format("the server answered HTTP %d instead of upgrading to a WebSocket", status);
    _conn.close();
    return false;
  }
  _lastRx = nowMs();
  return true;
}

void WebSocket::close() {
  if (_conn.isOpen() && !_failed) {
    // Status 1000, normal closure. Best effort: the socket goes either way.
    uint8_t frame[HEADROOM + 2];
    frame[HEADROOM] = 0x03;
    frame[HEADROOM + 1] = 0xE8;
    sendFrame(OP_CLOSE, frame, 2);
  }
  _conn.close();
  _rx.clear();
  _message.clear();
  _failed = false;
}

uint32_t WebSocket::idleMs() const {
  return nowMs() - _lastRx;
}

bool WebSocket::poll(const TextHandler &onText) {
  if (!connected()) return false;
  // A few reads at most, so one busy connection cannot hold loop() for long.
  for (int i = 0; i < 4; i++) {
    const size_t had = _rx.size();
    if (had > CLAVDA_WS_MAX_MESSAGE + HEADROOM) {
      CLAVDA_LOGW("A message from the relay is over %u bytes; reconnecting",
                  (unsigned)CLAVDA_WS_MAX_MESSAGE);
      _failed = true;
      return false;
    }
    _rx.resize(had + 1024);
    const int n = _conn.read((uint8_t *)&_rx[had], 1024);
    _rx.resize(had + (n > 0 ? (size_t)n : 0));
    if (n < 0) {
      _failed = true;
      return false;
    }
    if (n == 0) break;
    _lastRx = nowMs();
    if (!parse(onText)) return false;
  }
  return connected();
}

bool WebSocket::parse(const TextHandler &onText) {
  size_t at = 0;
  bool ok = true;
  while (ok && _rx.size() - at >= 2) {
    const uint8_t *p = (const uint8_t *)_rx.data() + at;
    const size_t avail = _rx.size() - at;
    const bool fin = p[0] & 0x80;
    const uint8_t opcode = p[0] & 0x0F;
    const bool masked = p[1] & 0x80;
    uint64_t len = p[1] & 0x7F;
    size_t header = 2;
    if (p[0] & 0x70) {
      CLAVDA_LOGW("The relay sent a WebSocket frame with reserved bits set");
      ok = false;
      break;
    }
    if (len == 126) {
      if (avail < 4) break;
      len = ((uint64_t)p[2] << 8) | p[3];
      header = 4;
    } else if (len == 127) {
      if (avail < 10) break;
      len = 0;
      for (int i = 0; i < 8; i++) len = (len << 8) | p[2 + i];
      header = 10;
    }
    if (len > CLAVDA_WS_MAX_MESSAGE) {
      CLAVDA_LOGW("A message from the relay is over %u bytes; reconnecting",
                  (unsigned)CLAVDA_WS_MAX_MESSAGE);
      ok = false;
      break;
    }
    const size_t maskAt = header;
    if (masked) header += 4;
    if (avail < header + len) break;

    uint8_t *payload = (uint8_t *)&_rx[at + header];
    if (masked) {
      const uint8_t *mask = (const uint8_t *)_rx.data() + at + maskAt;
      for (size_t i = 0; i < len; i++) payload[i] ^= mask[i & 3];
    }
    at += header + (size_t)len;

    switch (opcode) {
      case OP_TEXT:
      case OP_BINARY:
        if (fin) {
          if (opcode == OP_TEXT && at < _rx.size()) {
            // The terminator goes where the next frame starts; put that back after.
            const char saved = _rx[at];
            _rx[at] = 0;
            onText((const char *)payload, (size_t)len);
            _rx[at] = saved;
          } else if (opcode == OP_TEXT) {
            // The last frame read: std::string keeps a NUL after its last byte.
            onText((const char *)payload, (size_t)len);
          }
        } else {
          _messageOpcode = opcode;
          _message.assign((const char *)payload, (size_t)len);
        }
        break;
      case OP_CONTINUATION:
        if (!_messageOpcode || _message.size() + len > CLAVDA_WS_MAX_MESSAGE) {
          ok = false;
          break;
        }
        _message.append((const char *)payload, (size_t)len);
        if (fin) {
          if (_messageOpcode == OP_TEXT) onText(_message.c_str(), _message.size());
          _message.clear();
          _messageOpcode = 0;
        }
        break;
      case OP_PING: {
        // A pong carries the ping's payload back; control frames are at most 125 bytes.
        uint8_t frame[HEADROOM + 125];
        const size_t n = len < 125 ? (size_t)len : 125;
        memcpy(frame + HEADROOM, payload, n);
        if (!sendFrame(OP_PONG, frame, n)) ok = false;
        break;
      }
      case OP_PONG:
        break;
      case OP_CLOSE:
        // Answer it, then let the caller drop the connection.
        ok = false;
        break;
      default:
        ok = false;
        break;
    }
    // A handler may have sent something that failed.
    if (_failed) ok = false;
  }
  _rx.erase(0, at);
  if (ok) return true;
  // A close from the server or a protocol error: answer with a close frame of
  // our own. After a send that failed, there is nothing left to answer on.
  if (!_failed) close();
  _failed = true;
  return false;
}

bool WebSocket::sendFrame(uint8_t opcode, uint8_t *buf, size_t len) {
  if (!connected()) return false;
  uint8_t *payload = buf + HEADROOM;
  const size_t lenBytes = len < 126 ? 0 : (len <= 0xFFFF ? 2 : 8);
  const size_t header = 2 + lenBytes + 4;
  uint8_t *h = payload - header;
  h[0] = 0x80 | opcode;  // always the whole message in one frame
  if (lenBytes == 0) {
    h[1] = 0x80 | (uint8_t)len;
  } else if (lenBytes == 2) {
    h[1] = 0x80 | 126;
    h[2] = (uint8_t)(len >> 8);
    h[3] = (uint8_t)len;
  } else {
    h[1] = 0x80 | 127;
    for (int i = 0; i < 8; i++) h[2 + i] = (uint8_t)((uint64_t)len >> (56 - 8 * i));
  }
  // Client frames are always masked (RFC 6455, 5.3).
  uint8_t *mask = h + 2 + lenBytes;
  esp_fill_random(mask, 4);
  for (size_t i = 0; i < len; i++) payload[i] ^= mask[i & 3];

  if (!_conn.write(h, header + len, WRITE_TIMEOUT_MS)) {
    _failed = true;
    return false;
  }
  return true;
}

bool WebSocket::sendText(uint8_t *buf, size_t len) {
  return sendFrame(OP_TEXT, buf, len);
}

bool WebSocket::sendText(const char *text, size_t len) {
  std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[HEADROOM + len]);
  if (!buf) return false;
  memcpy(buf.get() + HEADROOM, text, len);
  return sendFrame(OP_TEXT, buf.get(), len);
}

bool WebSocket::ping() {
  uint8_t frame[HEADROOM];
  return sendFrame(OP_PING, frame, 0);
}

}  // namespace clavda
