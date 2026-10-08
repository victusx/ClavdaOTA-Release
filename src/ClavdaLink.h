#ifndef ClavdaLink_h
#define ClavdaLink_h

#include <Arduino.h>
#include <functional>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>

namespace clavda {

/**
 * The device's one outbound connection to the relay: Socket.IO v4 framing
 * (Engine.IO 4, websocket transport only) over a TLS WebSocket.
 *
 * Written here rather than on the WebSockets library's SocketIOclient, which
 * sends the Engine.IO 3 "2probe" and client-side pings on connect; Engine.IO 4
 * servers treat a client ping as a protocol error. Only the handful of packet
 * types the relay uses are implemented:
 *
 *   server -> device   0{open}  2 ping  40{connected}  42[event,data]  44 connect_error  41 / 1 close
 *   device -> server   40{auth}  3 pong  42[event,data]
 */
class Link {
  public:
    using EventHandler = std::function<void(const char *event, JsonVariantConst data)>;

    /** `authJson` is called on every dial, so it always carries the current versions. */
    bool begin(const String &relayUrl, const char *caRoots, std::function<String()> authJson,
               EventHandler onEvent, std::function<void()> onOnline);
    void loop();
    /** Close cleanly and stop redialling. */
    void stop();

    /** The Socket.IO namespace is connected - events can be sent. */
    bool online() const { return _ioOnline; }
    /** The relay refused the credentials as non-retryable; redialled every 15 minutes. */
    bool rejected() const { return _rejected; }

    /** Spare bytes in front of a packet passed to send(), for the WebSocket header. */
    static const size_t HEADROOM = WEBSOCKETS_MAX_HEADER_SIZE;

    /** Send `42["event",<data>]`. `dataJson` must already be serialized JSON. */
    bool emit(const char *event, const String &dataJson);
    /**
     * Send a Socket.IO text packet laid out at `buf + HEADROOM`, `len` bytes
     * long. The WebSocket header is written into the headroom, so the packet
     * goes out in one write and is never copied.
     */
    bool send(uint8_t *buf, size_t len);

  private:
    void onWsEvent(WStype_t type, uint8_t *payload, size_t length);
    void onText(const char *text, size_t length);
    void onSocketIoEvent(const char *json, size_t length);
    void scheduleRedial();

    WebSocketsClient _ws;
    std::function<String()> _authJson;
    EventHandler _onEvent;
    std::function<void()> _onOnline;

    bool _started = false;
    bool _stopped = false;
    bool _wsOpen = false;
    bool _ioOnline = false;
    bool _rejected = false;
    uint32_t _attempt = 0;
    uint32_t _redialDelay = 0;
    uint32_t _redialScheduledAt = 0;
    uint32_t _onlineSince = 0;
    uint32_t _lastServerPing = 0;
    uint32_t _pingInterval = 25000;
    uint32_t _pingTimeout = 20000;
};

}  // namespace clavda

#endif
