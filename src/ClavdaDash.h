#ifndef ClavdaDash_h
#define ClavdaDash_h

#include <Arduino.h>
#include <ArduinoJson.h>
#include "ClavdaIdentity.h"
#include "ClavdaWidget.h"

/** Dashboards that may be open at once; each costs a send of every change. */
#ifndef CLAVDA_DASH_MAX_SESSIONS
  #define CLAVDA_DASH_MAX_SESSIONS 3
#endif

/** Longest control input a console may send, sealed JSON included. */
#ifndef CLAVDA_DASH_MAX_INPUT
  #define CLAVDA_DASH_MAX_INPUT 512
#endif

namespace clavda {

class Link;

/**
 * The relay side of ClavdaWidget: the dashboards open on this device. Each
 * comes from a session the console sealed with the job key, like a Serial
 * Monitor's, carrying its own AES key and whether it may use controls; each
 * keeps its own place in the stream of changes, and lapses unless renewed.
 *
 *   dash_start { sessionId, enc, full? }    open or renew; full asks for everything again
 *   dash_input { sessionId, seq, data }     sealed {"id","v"}, aad "in.<seq>"
 *   dash_stop  { sessionId }
 *   -> send_to_web dash_update { boot, seq, data }   sealed, aad "dash.<boot>.<seq>"
 *
 * An update is {"full"?:1, "v":{id:value}, "p":{id:[[value, ageMs], ...]}}:
 * values are the latest of each widget, points are line-chart points with how
 * long ago each was added, so the console can time them without a clock here.
 */
class Dash {
  public:
    /** Handle a dash_* command into `reply`. False if `command` is not one. */
    bool handle(const char *command, JsonVariantConst payload, Identity &identity,
                JsonDocument &reply);
    /** Send each open dashboard what changed. Call from loop(). */
    void pump(Link &link);
    /** Widgets the sketch declared. */
    static size_t widgets();

  private:
    /** How far a console is through the stream of changes. */
    struct Place {
      /** Send every value and the chart history before anything else. */
      bool full = true;
      /** Changes up to this generation are on that console. */
      uint32_t sentGen = 0;
      /** Chart points before this index are on that console. */
      uint32_t pointsSent = 0;
      /** A pass over the widgets that did not fit one frame, and where it stands. */
      bool inPass = false;
      uint32_t passFrom = 0;
      uint32_t passTo = 0;
      size_t cursor = 0;
      /** ClavdaWidget::_epoch the pass was made against. */
      uint32_t epoch = 0;
    };

    struct Session {
      bool active = false;
      bool input = false;
      char id[65] = {0};
      uint8_t key[32] = {0};
      uint32_t inSeq = 0;
      uint32_t outSeq = 0;
      uint32_t leaseUntil = 0;
      uint32_t lastFlush = 0;
      Place place;
    };

    /** Allocated when the first dashboard opens and freed when the last one goes. */
    struct Open {
      Session sessions[CLAVDA_DASH_MAX_SESSIONS];
    };

    void start(JsonVariantConst payload, Identity &identity, JsonDocument &reply);
    void input(JsonVariantConst payload, JsonDocument &reply);
    void stop(JsonVariantConst payload, JsonDocument &reply);
    /**
     * Build the next frame into `doc`, advancing `place` past what it holds.
     * The caller passes a copy and keeps it only once the frame is sent.
     */
    static bool nextFrame(Place &place, JsonDocument &doc, uint32_t now);
    static void writeValue(JsonObject values, const ClavdaWidget &widget);
    bool send(Link &link, Session &session, const JsonDocument &doc);
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
