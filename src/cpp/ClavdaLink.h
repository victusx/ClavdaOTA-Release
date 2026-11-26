#ifndef ClavdaLink_h
#define ClavdaLink_h

#include <functional>
#include <string>
#include <ArduinoJson.h>
#include "ClavdaNet.h"

namespace clavda {

/**
 * The device's one outbound connection to the relay: Socket.IO v4 framing
 * (Engine.IO 4, websocket transport only) over a TLS WebSocket.
 *
 * Only the handful of packet types the relay uses are implemented:
 *
 *   server -> device   0{open}  2 ping  40{connected}  42[event,data]  44 connect_error  41 / 1 close
 *   device -> server   40{auth}  3 pong  42[event,data]
 *
 * Engine.IO 4 servers drive the heartbeat themselves and treat a client
 * ping as a protocol error, so the device only ever answers.
 */
class Link {
  public:
    using EventHandler = std::function<void(const char *event, JsonVariantConst data)>;

    /** `authJson` is called on every dial, so it always carries the current versions. */
    bool begin(const std::string &relayUrl, const char *caRoots,
               std::function<std::string()> authJson, EventHandler onEvent,
               std::function<void()> onOnline);
    void loop();
    /** Close cleanly and stop redialling. */
    void stop();

    /** The Socket.IO namespace is connected - events can be sent. */
    bool online() const { return _ioOnline; }
    /** The relay refused the credentials as non-retryable; redialled every 15 minutes. */
    bool rejected() const { return _rejected; }

    /** Spare bytes in front of a packet passed to send(), for the WebSocket header. */
    static const size_t HEADROOM = WebSocket::HEADROOM;

    /** Send `42["event",<data>]`. `dataJson` must already be serialized JSON. */
    bool emit(const char *event, const std::string &dataJson);
    /**
     * Send a Socket.IO text packet laid out at `buf + HEADROOM`, `len` bytes
     * long. The WebSocket header is written into the headroom, so the packet
     * goes out in one write and is never copied.
     */
    bool send(uint8_t *buf, size_t len);

  private:
    void dial();
    void dropped();
    void onText(const char *text, size_t length);
    void onSocketIoEvent(const char *json, size_t length);
    void scheduleRedial();

    WebSocket _ws;
    Url _url;
    const char *_caRoots = nullptr;
    std::function<std::string()> _authJson;
    EventHandler _onEvent;
    std::function<void()> _onOnline;

    bool _started = false;
    bool _stopped = false;
    bool _ioOnline = false;
    bool _rejected = false;
    /** Set while reading; the connection is dropped once the read is done. */
    bool _closeRequested = false;
    uint32_t _attempt = 0;
    uint32_t _redialDelay = 0;
    uint32_t _redialScheduledAt = 0;
    uint32_t _onlineSince = 0;
    uint32_t _lastServerPing = 0;
    uint32_t _lastWsPing = 0;
    uint32_t _pingInterval = 25000;
    uint32_t _pingTimeout = 20000;
};

}  // namespace clavda

#endif
