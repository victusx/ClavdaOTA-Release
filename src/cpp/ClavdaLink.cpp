#include "ClavdaLink.h"

#include <string.h>
#include <memory>
#include "ClavdaPlatform.h"

namespace clavda {

// Redial backoff with full jitter - the same shape as the headless agent's, so
// a relay restart does not see every device come back on the same tick.
static const uint32_t BACKOFF_BASE_MS = 1000;
static const uint32_t BACKOFF_MAX_MS = 60000;
// Credentials the relay refused will not fix themselves; try again rarely.
static const uint32_t REJECTED_RETRY_MS = 15UL * 60UL * 1000UL;
// Online this long counts as recovered, and the backoff starts over.
static const uint32_t STABLE_MS = 30000;
// The connection, the TLS handshake and the WebSocket upgrade, each.
static const int DIAL_TIMEOUT_MS = 15000;
// WebSocket pings catch a half-open TCP connection long before the relay's
// Engine.IO ping would: after this long with nothing from the relay, ping...
static const uint32_t WS_IDLE_MS = 15000;
static const uint32_t WS_PING_EVERY_MS = 5000;
// ...and after this long, give up on the connection.
static const uint32_t WS_DEAD_MS = 25000;

bool Link::begin(const std::string &relayUrl, const char *caRoots,
                 std::function<std::string()> authJson, EventHandler onEvent,
                 std::function<void()> onOnline) {
  std::string url = relayUrl;
  while (!url.empty() && (url.back() == ' ' || url.back() == '\r' || url.back() == '\n')) {
    url.pop_back();
  }
  if (!parseUrl(url, _url)) {
    CLAVDA_LOGE("Relay URL must start with https:// or wss:// (http:// for local testing)");
    return false;
  }
  // Socket.IO lives under the relay's path.
  const size_t query = _url.path.find('?');
  if (query != std::string::npos) _url.path.resize(query);
  if (_url.path.empty() || _url.path.back() != '/') _url.path += '/';
  _url.path += "socket.io/?EIO=4&transport=websocket";

  if (_url.tls && (!caRoots || !caRoots[0])) {
    CLAVDA_LOGE("No CA roots in the bundle; refusing to connect without certificate checks");
    return false;
  }
  if (!_url.tls) CLAVDA_LOGW("Relay URL is not TLS - only use this against a local relay");

  _caRoots = caRoots;
  _authJson = authJson;
  _onEvent = onEvent;
  _onOnline = onOnline;
  // The first dial goes as soon as the network is up.
  _redialDelay = 0;
  _redialScheduledAt = nowMs();
  _started = true;
  _stopped = false;
  return true;
}

void Link::loop() {
  if (!_started || _stopped) return;
  const uint32_t now = nowMs();

  if (!_ws.connected()) {
    // Without a network every dial is a DNS failure; wait for the application to bring it up.
    if (!networkUp() || now - _redialScheduledAt < _redialDelay) return;
    dial();
    return;
  }

  _closeRequested = false;
  const bool alive = _ws.poll([this](const char *text, size_t length) { onText(text, length); });
  if (!alive || _closeRequested) {
    dropped();
    return;
  }

  const uint32_t idle = _ws.idleMs();
  if (idle > WS_DEAD_MS) {
    CLAVDA_LOGW("No reply from the relay; reconnecting");
    dropped();
    return;
  }
  if (idle > WS_IDLE_MS && now - _lastWsPing >= WS_PING_EVERY_MS) {
    _ws.ping();
    _lastWsPing = now;
  }
  if (now - _lastServerPing > _pingInterval + _pingTimeout + 5000) {
    CLAVDA_LOGW("No ping from the relay; reconnecting");
    dropped();
    return;
  }
  if (_ioOnline && _attempt && now - _onlineSince > STABLE_MS) _attempt = 0;
}

void Link::dial() {
  std::string error;
  if (!_ws.connect(_url, _caRoots, DIAL_TIMEOUT_MS, error)) {
    // Once per outage at warning level; the retries after it at debug.
    if (_attempt == 0 && !_rejected) {
      CLAVDA_LOGW("Could not reach the relay: %s. Retrying.", error.c_str());
    } else {
      CLAVDA_LOGD("Could not reach the relay: %s", error.c_str());
    }
    scheduleRedial();
    return;
  }
  // Nothing to send yet: the relay opens with an Engine.IO OPEN packet.
  _lastServerPing = nowMs();
  _lastWsPing = _lastServerPing;
}

void Link::dropped() {
  if (_ioOnline) CLAVDA_LOGI("Disconnected from the relay");
  _ws.close();
  _ioOnline = false;
  scheduleRedial();
}

void Link::stop() {
  _stopped = true;
  _ws.close();
  _ioOnline = false;
}

bool Link::emit(const char *event, const std::string &dataJson) {
  if (!_ioOnline) return false;
  const size_t eventLen = strlen(event);
  const size_t len = 4 + eventLen + 2 + dataJson.size() + 1;  // 42["event",data]
  std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[HEADROOM + len]);
  if (!buf) return false;
  char *p = (char *)buf.get() + HEADROOM;
  memcpy(p, "42[\"", 4);
  memcpy(p + 4, event, eventLen);
  memcpy(p + 4 + eventLen, "\",", 2);
  memcpy(p + 6 + eventLen, dataJson.data(), dataJson.size());
  p[len - 1] = ']';
  return send(buf.get(), len);
}

bool Link::send(uint8_t *buf, size_t len) {
  if (!_ioOnline) return false;
  return _ws.sendText(buf, len);
}

void Link::scheduleRedial() {
  uint32_t delayMs;
  if (_rejected) {
    delayMs = REJECTED_RETRY_MS;
  } else {
    uint32_t ceiling = BACKOFF_BASE_MS << (_attempt < 6 ? _attempt : 6);
    if (ceiling > BACKOFF_MAX_MS) ceiling = BACKOFF_MAX_MS;
    delayMs = BACKOFF_BASE_MS + randomBelow(ceiling);
    _attempt++;
  }
  _redialDelay = delayMs;
  _redialScheduledAt = nowMs();
}

void Link::onText(const char *text, size_t length) {
  if (!length) return;
  switch (text[0]) {
    case '0': {  // Engine.IO OPEN: {"sid","pingInterval","pingTimeout",...}
      JsonDocument open;
      if (!deserializeJson(open, text + 1, length - 1)) {
        _pingInterval = open["pingInterval"] | _pingInterval;
        _pingTimeout = open["pingTimeout"] | _pingTimeout;
      }
      _lastServerPing = nowMs();
      const std::string connect = "40" + _authJson();
      _ws.sendText(connect.data(), connect.size());
      break;
    }
    case '2':  // Engine.IO PING - the server drives the heartbeat in v4
      _lastServerPing = nowMs();
      _ws.sendText("3", 1);
      break;
    case '1':  // Engine.IO CLOSE
      _closeRequested = true;
      break;
    case '4': {  // Engine.IO MESSAGE carrying a Socket.IO packet
      if (length < 2) break;
      const char kind = text[1];
      if (kind == '0') {
        _ioOnline = true;
        _rejected = false;
        _onlineSince = nowMs();
        CLAVDA_LOGI("Connected to the relay");
        if (_onOnline) _onOnline();
      } else if (kind == '2') {
        onSocketIoEvent(text + 2, length - 2);
      } else if (kind == '4') {
        CLAVDA_LOGW("Relay refused the connection: %.*s", (int)(length - 2), text + 2);
        _closeRequested = true;
      } else if (kind == '1') {
        _closeRequested = true;
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
    CLAVDA_LOGW("Unreadable event from the relay: %s", err.c_str());
    return;
  }
  const char *event = doc[0] | "";
  JsonVariantConst data = doc[1];

  if (!strcmp(event, "auth_error")) {
    const bool retryable = data["retryable"] | false;
    if (!retryable) {
      _rejected = true;
      CLAVDA_LOGE("The relay rejected this device's credentials (%s). Retrying in 15 minutes.",
                  data["code"] | "AUTH_FAILED");
    } else {
      CLAVDA_LOGW("Relay auth failed (%s); will retry", data["code"] | "AUTH_FAILED");
    }
    return;
  }

  if (_onEvent) _onEvent(event, data);
}

}  // namespace clavda
