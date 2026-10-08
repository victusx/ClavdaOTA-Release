#include "ClavdaLink.h"

#include <WiFi.h>
#include <memory>

namespace clavda {

// Redial backoff with full jitter - the same shape as the headless agent's, so
// a relay restart does not see every device come back on the same tick.
static const uint32_t BACKOFF_BASE_MS = 1000;
static const uint32_t BACKOFF_MAX_MS = 60000;
// Credentials the relay refused will not fix themselves; try again rarely.
static const uint32_t REJECTED_RETRY_MS = 15UL * 60UL * 1000UL;
// Online this long counts as recovered, and the backoff starts over.
static const uint32_t STABLE_MS = 30000;

bool Link::begin(const String &relayUrl, const char *caRoots, std::function<String()> authJson,
                 EventHandler onEvent, std::function<void()> onOnline) {
  String url = relayUrl;
  url.trim();

  bool tls;
  if (url.startsWith("https://")) {
    tls = true;
    url.remove(0, 8);
  } else if (url.startsWith("wss://")) {
    tls = true;
    url.remove(0, 6);
  } else if (url.startsWith("http://")) {
    tls = false;
    url.remove(0, 7);
  } else if (url.startsWith("ws://")) {
    tls = false;
    url.remove(0, 5);
  } else {
    log_e("[ClavdaOTA] Relay URL must start with https:// or wss:// (http:// for local testing)");
    return false;
  }

  int slash = url.indexOf('/');
  String hostPort = slash >= 0 ? url.substring(0, slash) : url;
  String path = slash >= 0 ? url.substring(slash) : String("/");
  if (!path.endsWith("/")) path += "/";
  path += "socket.io/?EIO=4&transport=websocket";

  uint16_t port = tls ? 443 : 80;
  int colon = hostPort.indexOf(':');
  String host = colon >= 0 ? hostPort.substring(0, colon) : hostPort;
  if (colon >= 0) port = (uint16_t)hostPort.substring(colon + 1).toInt();
  if (!host.length() || !port) {
    log_e("[ClavdaOTA] Relay URL has no usable host");
    return false;
  }

  if (tls && (!caRoots || !caRoots[0])) {
    log_e("[ClavdaOTA] No CA roots in the bundle; refusing to connect without certificate checks");
    return false;
  }

  _authJson = authJson;
  _onEvent = onEvent;
  _onOnline = onOnline;

  _ws.onEvent([this](WStype_t type, uint8_t *payload, size_t length) {
    onWsEvent(type, payload, length);
  });
  // WebSocket-level pings catch a half-open TCP connection long before the
  // relay's Engine.IO ping would.
  _ws.enableHeartbeat(15000, 5000, 2);
  _redialDelay = BACKOFF_BASE_MS;
  _redialScheduledAt = millis();
  _ws.setReconnectInterval(BACKOFF_BASE_MS);

  if (tls) {
    _ws.beginSslWithCA(host.c_str(), port, path.c_str(), caRoots);
  } else {
    log_w("[ClavdaOTA] Relay URL is not TLS - only use this against a local relay");
    _ws.begin(host.c_str(), port, path.c_str());
  }

  _started = true;
  _stopped = false;
  return true;
}

void Link::loop() {
  if (!_started || _stopped) return;
  // Without Wi-Fi every dial is a DNS failure; wait for the sketch to bring it up.
  if (!_wsOpen && WiFi.status() != WL_CONNECTED) return;

  _ws.loop();

  const uint32_t now = millis();
  // The WebSockets library raises no event when a dial fails outright, so a
  // window that passes without a connection counts as a failed attempt.
  if (!_wsOpen && now - _redialScheduledAt >= _redialDelay) scheduleRedial();
  if (_wsOpen && now - _lastServerPing > _pingInterval + _pingTimeout + 5000) {
    log_w("[ClavdaOTA] No ping from the relay; reconnecting");
    _ws.disconnect();
  }
  if (_ioOnline && _attempt && now - _onlineSince > STABLE_MS) _attempt = 0;
}

void Link::stop() {
  _stopped = true;
  if (_wsOpen) _ws.disconnect();
  _wsOpen = false;
  _ioOnline = false;
}

bool Link::emit(const char *event, const String &dataJson) {
  if (!_ioOnline) return false;
  const size_t eventLen = strlen(event);
  const size_t len = 4 + eventLen + 2 + dataJson.length() + 1;  // 42["event",data]
  std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[HEADROOM + len]);
  if (!buf) return false;
  char *p = (char *)buf.get() + HEADROOM;
  memcpy(p, "42[\"", 4);
  memcpy(p + 4, event, eventLen);
  memcpy(p + 4 + eventLen, "\",", 2);
  memcpy(p + 6 + eventLen, dataJson.c_str(), dataJson.length());
  p[len - 1] = ']';
  return send(buf.get(), len);
}

bool Link::send(uint8_t *buf, size_t len) {
  if (!_ioOnline) return false;
  return _ws.sendTXT(buf, len, true);
}

void Link::scheduleRedial() {
  uint32_t delayMs;
  if (_rejected) {
    delayMs = REJECTED_RETRY_MS;
  } else {
    uint32_t ceiling = BACKOFF_BASE_MS << (_attempt < 6 ? _attempt : 6);
    if (ceiling > BACKOFF_MAX_MS) ceiling = BACKOFF_MAX_MS;
    delayMs = BACKOFF_BASE_MS + (uint32_t)random(0, ceiling);
    _attempt++;
  }
  _redialDelay = delayMs;
  _redialScheduledAt = millis();
  _ws.setReconnectInterval(delayMs);
}

void Link::onWsEvent(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      _wsOpen = true;
      _lastServerPing = millis();
      // Nothing to send yet: the relay opens with an Engine.IO OPEN packet.
      break;
    case WStype_DISCONNECTED:
      if (_ioOnline) log_i("[ClavdaOTA] Disconnected from the relay");
      _wsOpen = false;
      _ioOnline = false;
      scheduleRedial();
      break;
    case WStype_TEXT:
      if (payload && length) onText((const char *)payload, length);
      break;
    default:
      break;
  }
}

void Link::onText(const char *text, size_t length) {
  switch (text[0]) {
    case '0': {  // Engine.IO OPEN: {"sid","pingInterval","pingTimeout",...}
      JsonDocument open;
      if (!deserializeJson(open, text + 1, length - 1)) {
        _pingInterval = open["pingInterval"] | _pingInterval;
        _pingTimeout = open["pingTimeout"] | _pingTimeout;
      }
      _lastServerPing = millis();
      String connect = "40" + _authJson();
      _ws.sendTXT(connect);
      break;
    }
    case '2':  // Engine.IO PING - the server drives the heartbeat in v4
      _lastServerPing = millis();
      _ws.sendTXT("3");
      break;
    case '1':  // Engine.IO CLOSE
      _ws.disconnect();
      break;
    case '4': {  // Engine.IO MESSAGE carrying a Socket.IO packet
      if (length < 2) break;
      const char kind = text[1];
      if (kind == '0') {
        _ioOnline = true;
        _rejected = false;
        _onlineSince = millis();
        log_i("[ClavdaOTA] Connected to the relay");
        if (_onOnline) _onOnline();
      } else if (kind == '2') {
        onSocketIoEvent(text + 2, length - 2);
      } else if (kind == '4') {
        log_w("[ClavdaOTA] Relay refused the connection: %.*s", (int)(length - 2), text + 2);
        _ws.disconnect();
      } else if (kind == '1') {
        _ws.disconnect();
      }
      break;
    }
    default:
      break;
  }
}

void Link::onSocketIoEvent(const char *json, size_t length) {
  // An ack id may precede the array; the relay never asks for acks, but skip
  // one rather than misparse.
  size_t i = 0;
  while (i < length && json[i] >= '0' && json[i] <= '9') i++;
  if (i >= length || json[i] != '[') return;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json + i, length - i);
  if (err) {
    log_w("[ClavdaOTA] Unreadable event from the relay: %s", err.c_str());
    return;
  }
  const char *event = doc[0] | "";
  JsonVariantConst data = doc[1];

  if (!strcmp(event, "auth_error")) {
    const bool retryable = data["retryable"] | false;
    if (!retryable) {
      _rejected = true;
      log_e("[ClavdaOTA] The relay rejected this device's credentials (%s). Retrying in 15 minutes.",
            data["code"] | "AUTH_FAILED");
    } else {
      log_w("[ClavdaOTA] Relay auth failed (%s); will retry", data["code"] | "AUTH_FAILED");
    }
    return;
  }

  if (_onEvent) _onEvent(event, data);
}

}  // namespace clavda
