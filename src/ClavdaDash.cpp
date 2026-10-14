#include "ClavdaDash.h"

#include <inttypes.h>
#include <memory>
#include "esp_idf_version.h"
#if ESP_IDF_VERSION_MAJOR >= 5
  #include "esp_random.h"
#else
  #include "esp_system.h"
#endif

#include "ClavdaCrypto.h"
#include "ClavdaLink.h"
#include "ClavdaSession.h"

namespace clavda {

// The console renews every 20 s, so a closed tab stops costing anything
// within a minute even if its dash_stop never arrived.
static const uint32_t LEASE_MS = 60000;
// Folds a burst of set() calls into one frame; a gauge still moves smoothly.
static const uint32_t FLUSH_MS = 100;
// Plaintext per frame. Text and series are capped, so any one widget fits.
static const size_t FRAME_MAX = 1536;

uint32_t Dash::boot() {
  // Drawn lazily, by which time Wi-Fi is up and the RNG has real entropy.
  if (!_boot) _boot = esp_random() | 1u;
  return _boot;
}

size_t Dash::widgets() {
  size_t n = 0;
  for (ClavdaWidget *w = ClavdaWidget::_head; w; w = w->_next) n++;
  return n;
}

Dash::Session *Dash::find(const char *id) {
  if (!_open) return nullptr;
  for (Session &s : _open->sessions) {
    if (s.active && !strcmp(s.id, id)) return &s;
  }
  return nullptr;
}

void Dash::close(Session &session) {
  session = Session();
  releaseIfIdle();
}

void Dash::releaseIfIdle() {
  if (!_open) return;
  for (const Session &s : _open->sessions) {
    if (s.active) return;
  }
  delete _open;
  _open = nullptr;
}

void Dash::expire() {
  if (!_open) return;
  const uint32_t now = millis();
  for (Session &s : _open->sessions) {
    if (s.active && (int32_t)(now - s.leaseUntil) > 0) close(s);
    // The last close frees the sessions this loop is walking.
    if (!_open) return;
  }
}

bool Dash::handle(const char *command, JsonVariantConst payload, Identity &identity,
                  JsonDocument &reply) {
  if (!strcmp(command, "dash_start")) {
    start(payload, identity, reply);
  } else if (!strcmp(command, "dash_input")) {
    input(payload, reply);
  } else if (!strcmp(command, "dash_stop")) {
    stop(payload, reply);
  } else {
    return false;
  }
  return true;
}

void Dash::start(JsonVariantConst payload, Identity &identity, JsonDocument &reply) {
  auto refuse = [&reply](const char *code, const String &why) {
    reply["success"] = false;
    reply["code"] = code;
    reply["error"] = why;
  };

  const char *sessionId = payload["sessionId"] | "";
  SessionGrant grant;
  const char *code = nullptr;
  String why;
  if (!openSession("dash", "dashboard session", sessionId, payload["enc"] | "", identity, grant,
                   code, why)) {
    return refuse(code, why);
  }

  expire();
  Session *session = find(sessionId);
  const bool renewed = session != nullptr;
  if (!session) {
    if (!_open) _open = new (std::nothrow) Open();
    if (!_open) {
      memset(grant.key, 0, sizeof(grant.key));
      return refuse("busy", "Not enough free memory on the device to open a dashboard");
    }
    for (Session &s : _open->sessions) {
      if (!s.active) {
        session = &s;
        break;
      }
    }
    if (!session) {
      memset(grant.key, 0, sizeof(grant.key));
      return refuse("busy", String(CLAVDA_DASH_MAX_SESSIONS) +
                                " dashboards are already open on this device. Close one and try "
                                "again.");
    }
    session->active = true;
    strlcpy(session->id, sessionId, sizeof(session->id));
  }

  memcpy(session->key, grant.key, sizeof(session->key));
  memset(grant.key, 0, sizeof(grant.key));
  session->input = grant.input;
  session->leaseUntil = millis() + LEASE_MS;
  // A console that may have missed frames - its relay connection dropped,
  // say - asks for everything again.
  if (payload["full"] | false) {
    session->place.full = true;
    session->place.inPass = false;
  }

  reply["success"] = true;
  reply["sessionId"] = session->id;
  reply["boot"] = boot();
  reply["input"] = session->input;
  reply["renewed"] = renewed;
  reply["widgets"] = widgets();
}

void Dash::input(JsonVariantConst payload, JsonDocument &reply) {
  auto refuse = [&reply](const char *code, const String &why) {
    reply["success"] = false;
    reply["code"] = code;
    reply["error"] = why;
  };

  expire();
  Session *session = find(payload["sessionId"] | "");
  if (!session) return refuse("no_session", "This dashboard is not open on the device");
  if (!session->input) {
    return refuse("read_only", "Using controls needs permission to edit devices");
  }

  const uint32_t seq = payload["seq"] | 0u;
  if (seq <= session->inSeq) return refuse("replay", "This input was already delivered");

  char aad[16];
  snprintf(aad, sizeof(aad), "in.%" PRIu32, seq);
  uint8_t plain[CLAVDA_DASH_MAX_INPUT + 1];
  size_t len = 0;
  if (!openBytes(session->key, payload["data"] | "", aad, plain, CLAVDA_DASH_MAX_INPUT, len)) {
    return refuse("invalid", "The input did not verify, or is too long");
  }
  session->inSeq = seq;
  session->leaseUntil = millis() + LEASE_MS;

  JsonDocument doc;
  if (deserializeJson(doc, (const char *)plain, len)) return refuse("invalid", "Malformed input");
  const char *id = doc["id"] | "";
  ClavdaWidget *widget = ClavdaWidget::_head;
  while (widget && strcmp(widget->_id, id)) widget = widget->_next;
  if (!widget) {
    return refuse("unknown_widget", String("This firmware has no widget \"") + id + "\"");
  }

  // The widget takes the value first, so every open dashboard shows it even
  // if the handler does nothing; the handler can set() it again to override.
  JsonVariantConst value = doc["v"];
  ClavdaInput in;
  if (value.is<bool>()) {
    in._kind = ClavdaInput::BOOL;
    in._bool = value.as<bool>();
    widget->set(in._bool);
  } else if (value.is<double>()) {
    in._kind = ClavdaInput::NUMBER;
    in._number = value.as<double>();
    widget->set(in._number);
  } else if (value.is<const char *>()) {
    in._kind = ClavdaInput::TEXT;
    in._text = value.as<const char *>();
    widget->set(in._text);
  } else {
    return refuse("invalid", "A control sends a boolean, a number or text");
  }

  reply["success"] = true;
  if (widget->_onChange) widget->_onChange(in);
}

void Dash::stop(JsonVariantConst payload, JsonDocument &reply) {
  Session *session = find(payload["sessionId"] | "");
  if (session) close(*session);
  reply["success"] = true;
}

void Dash::pump(Link &link) {
  expire();
  if (!_open || !link.online()) return;

  const uint32_t now = millis();
  for (Session &s : _open->sessions) {
    if (!s.active || now - s.lastFlush < FLUSH_MS) continue;
    Place place = s.place;
    JsonDocument doc;
    if (!nextFrame(place, doc, now)) continue;
    // Not sent means the link is down or memory is short; the same changes
    // go in the next try.
    if (!send(link, s, doc)) break;
    s.place = place;
    s.lastFlush = now;
  }
}

void Dash::writeValue(JsonObject values, const ClavdaWidget &w) {
  switch (w._kind) {
    case ClavdaWidget::NUMBER:
      // A float written as a double would carry its rounding noise: 23.4 -> 23.39999962.
      if (w._float) {
        values[w._id] = (float)w._number;
      } else {
        values[w._id] = w._number;
      }
      break;
    case ClavdaWidget::BOOL:
      values[w._id] = w._bool;
      break;
    case ClavdaWidget::TEXT:
      if (w._level == CLAVDA_NEUTRAL) {
        values[w._id] = w._text;
      } else {
        JsonObject status = values[w._id].to<JsonObject>();
        status["t"] = w._text;
        status["l"] = (uint8_t)w._level;
      }
      break;
    case ClavdaWidget::SERIES: {
      JsonObject series = values[w._id].to<JsonObject>();
      JsonArray y = series["y"].to<JsonArray>();
      for (size_t i = 0; i < w._count; i++) y.add(w._values[i]);
      if (w._labels) {
        JsonArray labels = series["l"].to<JsonArray>();
        for (size_t i = 0; i < w._count; i++) labels.add(w._labels[i] ? w._labels[i] : "");
      }
      break;
    }
    default:
      break;
  }
}

bool Dash::nextFrame(Place &p, JsonDocument &doc, uint32_t now) {
  // A widget came or went since this pass began: start over, so none is skipped.
  if (p.epoch != ClavdaWidget::_epoch) {
    p.epoch = ClavdaWidget::_epoch;
    p.full = true;
    p.inPass = false;
  }

  bool any = false;
  const uint32_t generation = ClavdaWidget::_generation;
  if (!p.inPass && (p.full || p.sentGen != generation)) {
    p.inPass = true;
    p.passFrom = p.full ? 0 : p.sentGen;
    p.passTo = generation;
    p.cursor = 0;
    if (p.full) {
      // The console starts its charts over from the history still held here.
      const uint32_t head = ClavdaWidget::_pointHead;
      p.pointsSent = head > CLAVDA_DASH_POINTS ? head - CLAVDA_DASH_POINTS : 0;
      p.full = false;
      doc["full"] = 1;
      any = true;
    }
  }

  if (p.inPass) {
    JsonObject values = doc["v"].to<JsonObject>();
    ClavdaWidget *w = ClavdaWidget::_head;
    size_t i = 0;
    for (; w && i < p.cursor; w = w->_next) i++;
    for (; w; w = w->_next, i++) {
      if (w->_kind != ClavdaWidget::NONE && w->_gen > p.passFrom) {
        writeValue(values, *w);
        // The rest of the pass goes in the next frame. Never an empty frame, though.
        if (measureJson(doc) > FRAME_MAX && values.size() > 1) {
          values.remove(w->_id);
          break;
        }
        any = true;
      }
      p.cursor = i + 1;
    }
    if (!w) {
      p.inPass = false;
      p.sentGen = p.passTo;
    }
    if (!values.size()) doc.remove("v");
  }

  const uint32_t head = ClavdaWidget::_pointHead;
  const uint32_t oldest = head > CLAVDA_DASH_POINTS ? head - CLAVDA_DASH_POINTS : 0;
  // Points overwritten before they could be sent are gone; carry on from what is left.
  if (p.pointsSent < oldest) p.pointsSent = oldest;
  if (p.pointsSent < head && ClavdaWidget::_points) {
    JsonObject points = doc["p"].to<JsonObject>();
    while (p.pointsSent < head) {
      const ClavdaWidget::Point &point = ClavdaWidget::_points[p.pointsSent % CLAVDA_DASH_POINTS];
      if (point.widget) {
        JsonArray series = points[point.widget->_id];
        if (series.isNull()) series = points[point.widget->_id].to<JsonArray>();
        JsonArray entry = series.add<JsonArray>();
        entry.add(point.value);
        // How long ago, not when: the console turns it into a time with its own clock.
        entry.add(now - point.at);
        if (measureJson(doc) > FRAME_MAX && any) {
          series.remove(series.size() - 1);
          if (!series.size()) points.remove(point.widget->_id);
          break;
        }
        any = true;
      }
      p.pointsSent++;
    }
    if (!points.size()) doc.remove("p");
  }
  return any;
}

/**
 * One dash_update packet, sealed under the session's key and base64'd straight
 * into the packet that goes on the wire:
 *
 *   42["send_to_web",{"type":"dash_update","sessionId":"...","payload":{"boot":...,"seq":...,"data":"..."}}]
 */
bool Dash::send(Link &link, Session &session, const JsonDocument &doc) {
  const size_t len = measureJson(doc);
  std::unique_ptr<char[]> plain(new (std::nothrow) char[len + 1]);
  if (!plain) return false;
  serializeJson(doc, plain.get(), len + 1);

  // The tag covers the frame's place in the stream, so the relay cannot
  // replay or reorder frames without the console noticing.
  const uint32_t seq = session.outSeq + 1;
  char aad[40];
  snprintf(aad, sizeof(aad), "dash.%" PRIu32 ".%" PRIu32, boot(), seq);
  const size_t sealedLen = len + SEAL_OVERHEAD;
  std::unique_ptr<uint8_t[]> sealed(new (std::nothrow) uint8_t[sealedLen]);
  if (!sealed || !sealBytes(session.key, (const uint8_t *)plain.get(), len, aad, sealed.get())) {
    return false;
  }
  plain.reset();

  // Session ids are [A-Za-z0-9_-], so nothing here needs JSON escaping.
  char head[224];
  const int headLen = snprintf(head, sizeof(head),
                               "42[\"send_to_web\",{\"type\":\"dash_update\",\"sessionId\":\"%s\","
                               "\"payload\":{\"boot\":%" PRIu32 ",\"seq\":%" PRIu32 ",\"data\":\"",
                               session.id, boot(), seq);
  if (headLen <= 0 || headLen >= (int)sizeof(head)) return false;
  static const char TAIL[] = "\"}}]";
  const size_t dataLen = base64Length(sealedLen);
  const size_t packetLen = headLen + dataLen + sizeof(TAIL) - 1;

  // +1: base64Encode() NUL-terminates, and TAIL then overwrites it.
  std::unique_ptr<uint8_t[]> packet(new (std::nothrow) uint8_t[Link::HEADROOM + packetLen + 1]);
  if (!packet) return false;
  char *out = (char *)packet.get() + Link::HEADROOM;
  memcpy(out, head, headLen);
  size_t written = 0;
  if (!base64Encode(sealed.get(), sealedLen, out + headLen, dataLen + 1, written) ||
      written != dataLen) {
    return false;
  }
  memcpy(out + headLen + dataLen, TAIL, sizeof(TAIL) - 1);
  sealed.reset();
  if (!link.send(packet.get(), packetLen)) return false;
  session.outSeq = seq;
  return true;
}

}  // namespace clavda
