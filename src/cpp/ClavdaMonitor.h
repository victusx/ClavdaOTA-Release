#ifndef ClavdaMonitor_h
#define ClavdaMonitor_h

#include <Arduino.h>
#include <ArduinoJson.h>
#include "ClavdaIdentity.h"

/** Consoles that may watch at once; each costs a send of every chunk. */
#ifndef CLAVDA_SERIAL_MAX_SESSIONS
  #define CLAVDA_SERIAL_MAX_SESSIONS 3
#endif

/** Largest piece of output in one frame. */
#ifndef CLAVDA_SERIAL_CHUNK
  #define CLAVDA_SERIAL_CHUNK 1024
#endif

/** Longest line a console may send to onMessage(). */
#ifndef CLAVDA_SERIAL_MAX_INPUT
  #define CLAVDA_SERIAL_MAX_INPUT 512
#endif

namespace clavda {

class Link;

/**
 * The relay side of ClavdaSerial: the Serial Monitor sessions open on this
 * device. Each one comes from a session the console sealed with the job key,
 * carrying its own AES key and whether it may send input; each keeps its own
 * place in the output, and lapses unless the console renews it.
 *
 *   serial_start { sessionId, enc, boot?, from? }   open or renew; from resumes a stream
 *   serial_input { sessionId, seq, data }           a line for onMessage(), sealed, aad "in.<seq>"
 *   serial_stop  { sessionId }
 *   -> send_to_web serial_output { boot, off, data }  sealed output, aad "<boot>.<off>"
 *
 * `boot` is random per boot and `off` counts bytes since it, so a console can
 * tell a gap (overwritten output) from a restart, and drop a repeated frame.
 */
class Monitor {
  public:
    /** Handle a serial_* command into `reply`. False if `command` is not one. */
    bool handle(const char *command, JsonVariantConst payload, Identity &identity,
                JsonDocument &reply);
    /** Send what each session has not seen yet, and let lapsed sessions go. Call from loop(). */
    void pump(Link &link);
    /** The sketch started ClavdaSerial, so monitors can open. */
    bool available() const;

  private:
    struct Session {
      bool active = false;
      bool input = false;
      char id[65] = {0};
      uint8_t key[32] = {0};
      uint64_t sent = 0;
      uint32_t inSeq = 0;
      uint32_t leaseUntil = 0;
      uint32_t lastFlush = 0;
    };

    /** Allocated when the first monitor opens and freed when the last one goes. */
    struct Open {
      Session sessions[CLAVDA_SERIAL_MAX_SESSIONS];
      uint8_t chunk[CLAVDA_SERIAL_CHUNK];
    };

    void start(JsonVariantConst payload, Identity &identity, JsonDocument &reply);
    void input(JsonVariantConst payload, JsonDocument &reply);
    void stop(JsonVariantConst payload, JsonDocument &reply);
    bool sendOutput(Link &link, const Session &session, uint64_t off, size_t len);
    Session *find(const char *id);
    void close(Session &session);
    void expire();
    void releaseIfIdle();
    uint32_t boot();

    Open *_open = nullptr;
    uint32_t _boot = 0;
};

}  // namespace clavda

#endif
