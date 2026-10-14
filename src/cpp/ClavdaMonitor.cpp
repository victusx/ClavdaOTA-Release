#include "ClavdaMonitor.h"

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
#include "ClavdaSerial.h"
#include "ClavdaSession.h"

namespace clavda {

// The console renews every 20 s, so a closed tab stops costing anything
// within a minute even if its serial_stop never arrived.
static const uint32_t LEASE_MS = 60000;
// Batches a burst of prints into one frame without delaying a lone line much.
static const uint32_t FLUSH_MS = 50;

bool Monitor::available() const {
  return ClavdaSerial._buf != nullptr;
}

uint32_t Monitor::boot() {
  // Drawn lazily, by which time Wi-Fi is up and the RNG has real entropy.
  if (!_boot) _boot = esp_random() | 1u;
  return _boot;
}

Monitor::Session *Monitor::find(const char *id) {
  if (!_open) return nullptr;
  for (Session &s : _open->sessions) {
    if (s.active && !strcmp(s.id, id)) return &s;
  }
  return nullptr;
}

void Monitor::close(Session &session) {
  session = Session();
  releaseIfIdle();
}

void Monitor::releaseIfIdle() {
  if (!_open) return;
  for (const Session &s : _open->sessions) {
    if (s.active) return;
  }
  delete _open;
  _open = nullptr;
}

void Monitor::expire() {
  if (!_open) return;
  const uint32_t now = millis();
  for (Session &s : _open->sessions) {
    if (s.active && (int32_t)(now - s.leaseUntil) > 0) close(s);
    // The last close frees the sessions this loop is walking.
    if (!_open) return;
  }
}

bool Monitor::handle(const char *command, JsonVariantConst payload, Identity &identity,
                     JsonDocument &reply) {
  if (!strcmp(command, "serial_start")) {
    start(payload, identity, reply);
  } else if (!strcmp(command, "serial_input")) {
    input(payload, reply);
  } else if (!strcmp(command, "serial_stop")) {
    stop(payload, reply);
  } else {
    return false;
  }
  return true;
}

void Monitor::start(JsonVariantConst payload, Identity &identity, JsonDocument &reply) {
  auto refuse = [&reply](const char *code, const String &why) {
    reply["success"] = false;
    reply["code"] = code;
    reply["error"] = why;
  };

  if (!available()) {
    return refuse("not_started",
                  "This firmware does not start ClavdaSerial. Call ClavdaSerial.begin() in setup() "
                  "and print to ClavdaSerial.");
  }

  const char *sessionId = payload["sessionId"] | "";
  SessionGrant grant;
  const char *code = nullptr;
  String why;
  if (!openSession("serial", "monitor session", sessionId, payload["enc"] | "", identity, grant,
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
      return refuse("busy", "Not enough free memory on the device to open a Serial Monitor");
    }
    for (Session &s : _open->sessions) {
      if (!s.active) {
        session = &s;
        break;
      }
    }
    if (!session) {
      memset(grant.key, 0, sizeof(grant.key));
      return refuse("busy", String(CLAVDA_SERIAL_MAX_SESSIONS) +
                                " Serial Monitors are already open on this device. Close one and "
                                "try again.");
    }
    session->active = true;
    strlcpy(session->id, sessionId, sizeof(session->id));
    uint64_t head = 0;
    // A new monitor starts with everything still buffered.
    ClavdaSerial.window(session->sent, head);
  }

  memcpy(session->key, grant.key, sizeof(session->key));
  memset(grant.key, 0, sizeof(grant.key));
  session->input = grant.input;
  session->leaseUntil = millis() + LEASE_MS;

  // A console picking up where it left off - after its relay connection
  // dropped, say - gets exactly what it missed, if this boot still holds it.
  if (payload["boot"].as<uint32_t>() == boot() && payload["from"].is<uint64_t>()) {
    uint64_t oldest = 0;
    uint64_t head = 0;
    ClavdaSerial.window(oldest, head);
    const uint64_t from = payload["from"].as<uint64_t>();
    if (from <= head) session->sent = from;
  }

  // Output before this offset was printed before the monitor (re)opened, at
  // times this device cannot tell; the console leaves those lines unstamped.
  uint64_t oldest = 0;
  uint64_t head = 0;
  ClavdaSerial.window(oldest, head);

  reply["success"] = true;
  reply["sessionId"] = session->id;
  reply["boot"] = boot();
  reply["head"] = head;
  reply["input"] = session->input;
  reply["renewed"] = renewed;
}

void Monitor::input(JsonVariantConst payload, JsonDocument &reply) {
  auto refuse = [&reply](const char *code, const String &why) {
    reply["success"] = false;
    reply["code"] = code;
    reply["error"] = why;
  };

  expire();
  Session *session = find(payload["sessionId"] | "");
  if (!session) return refuse("no_session", "This Serial Monitor is not open on the device");
  if (!session->input) return refuse("read_only", "This Serial Monitor is view-only");

  const uint32_t seq = payload["seq"] | 0u;
  if (seq <= session->inSeq) return refuse("replay", "This input was already delivered");

  char aad[16];
  snprintf(aad, sizeof(aad), "in.%" PRIu32, seq);
  uint8_t line[CLAVDA_SERIAL_MAX_INPUT + 1];
  size_t len = 0;
  if (!openBytes(session->key, payload["data"] | "", aad, line, CLAVDA_SERIAL_MAX_INPUT, len)) {
    return refuse("invalid", String("The input did not verify, or is over ") +
                                 CLAVDA_SERIAL_MAX_INPUT + " bytes");
  }
  session->inSeq = seq;
  session->leaseUntil = millis() + LEASE_MS;
  line[len] = 0;  // onMessage() promises a terminated line; the C API passes it on as text

  reply["success"] = true;
  ClavdaSerial.deliver(line, len);
}

void Monitor::stop(JsonVariantConst payload, JsonDocument &reply) {
  Session *session = find(payload["sessionId"] | "");
  if (session) close(*session);
  reply["success"] = true;
}

void Monitor::pump(Link &link) {
  if (!available()) return;
  expire();

  size_t open = 0;
  const uint32_t now = millis();
  if (_open) {
    for (Session &s : _open->sessions) {
      if (!s.active) continue;
      open++;
      // Offline, output just accumulates; each session resumes from its own place.
      if (!link.online() || now - s.lastFlush < FLUSH_MS) continue;

      uint64_t from = s.sent;
      const size_t n = ClavdaSerial.read(from, _open->chunk, sizeof(_open->chunk));
      if (!n) {
        s.sent = from;
        continue;
      }
      // Not sent means the link is down or memory is short; the bytes stay
      // buffered for the next try.
      if (!sendOutput(link, s, from, n)) break;

      s.sent = from + n;
      s.lastFlush = now;
    }
  }
  ClavdaSerial._monitors = open;
}

/**
 * One serial_output packet. The output is sealed into a scratch buffer and
 * base64'd straight into the packet that goes on the wire, so a frame costs
 * two short-lived allocations and no copies of the text:
 *
 *   42["send_to_web",{"type":"serial_output","sessionId":"...","payload":{"boot":...,"off":...,"data":"..."}}]
 */
bool Monitor::sendOutput(Link &link, const Session &session, uint64_t off, size_t len) {
  // The tag covers where the bytes sit in the stream, so the relay cannot
  // replay or reorder frames without the console noticing.
  char aad[40];
  snprintf(aad, sizeof(aad), "%" PRIu32 ".%" PRIu64, boot(), off);
  const size_t sealedLen = len + SEAL_OVERHEAD;
  std::unique_ptr<uint8_t[]> sealed(new (std::nothrow) uint8_t[sealedLen]);
  if (!sealed || !sealBytes(session.key, _open->chunk, len, aad, sealed.get())) return false;

  // Session ids are [A-Za-z0-9_-], so nothing here needs JSON escaping.
  char head[224];
  const int headLen = snprintf(head, sizeof(head),
                               "42[\"send_to_web\",{\"type\":\"serial_output\",\"sessionId\":\"%s\","
                               "\"payload\":{\"boot\":%" PRIu32 ",\"off\":%" PRIu64 ",\"data\":\"",
                               session.id, boot(), off);
  if (headLen <= 0 || headLen >= (int)sizeof(head)) return false;
  static const char TAIL[] = "\"}}]";
  const size_t dataLen = base64Length(sealedLen);
  const size_t packetLen = headLen + dataLen + sizeof(TAIL) - 1;

  // +1: base64Encode() NUL-terminates, and TAIL then overwrites it.
  std::unique_ptr<uint8_t[]> packet(new (std::nothrow) uint8_t[Link::HEADROOM + packetLen + 1]);
  if (!packet) return false;
  char *p = (char *)packet.get() + Link::HEADROOM;
  memcpy(p, head, headLen);
  size_t written = 0;
  if (!base64Encode(sealed.get(), sealedLen, p + headLen, dataLen + 1, written) ||
      written != dataLen) {
    return false;
  }
  memcpy(p + headLen + dataLen, TAIL, sizeof(TAIL) - 1);
  sealed.reset();
  return link.send(packet.get(), packetLen);
}

}  // namespace clavda
