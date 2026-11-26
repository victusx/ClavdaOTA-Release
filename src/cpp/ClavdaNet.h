#ifndef ClavdaNet_h
#define ClavdaNet_h

#include "ClavdaMbedtlsConfig.h"  // first: esp_transport leads to mbedTLS

#include <stddef.h>
#include <stdint.h>
#include <functional>
#include <string>
#include "esp_transport.h"

/** Largest message the relay may send; a longer one closes the connection. */
#ifndef CLAVDA_WS_MAX_MESSAGE
  #define CLAVDA_WS_MAX_MESSAGE (15 * 1024)
#endif

namespace clavda {

/** An http(s):// or ws(s):// address, split the way a request needs it. */
struct Url {
  bool tls = false;
  std::string host;
  uint16_t port = 0;
  /** Path and query, as they go on the request line; "/" at least. */
  std::string path;

  /** The Host header: the host, and the port unless it is the scheme's default. */
  std::string hostHeader() const;
};

/** False unless `url` is http://, https://, ws:// or wss:// with a host. */
bool parseUrl(const std::string &url, Url &out);

/**
 * One TCP or TLS connection, on ESP-IDF's transport layer (tcp_transport),
 * which the Arduino core ships too. Reads never block, so loop() can poll
 * them; connecting and writing block, up to the timeout given.
 */
class Connection {
  public:
    Connection() = default;
    ~Connection() { close(); }
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;

    /**
     * A TLS certificate is always checked, against the PEM roots in
     * `caRoots`, and the host name against it. `error` says why not.
     */
    bool open(const Url &url, const char *caRoots, int timeoutMs, std::string &error);
    void close();
    bool isOpen() const { return _t != nullptr; }

    /** What has arrived, up to `cap` bytes: > 0 read, 0 nothing yet, -1 closed or failed. */
    int read(uint8_t *buf, size_t cap);
    /** All of `data`, or false: then the connection is no longer usable. */
    bool write(const void *data, size_t len, int timeoutMs);
    /**
     * An HTTP response head - the status line and the headers, up to the
     * blank line - waiting up to `timeoutMs`. Whatever came after it, the
     * start of the body, is left in `rest`.
     */
    bool readHead(int timeoutMs, std::string &head, std::string &rest, std::string &error);

  private:
    esp_transport_handle_t _t = nullptr;
};

/** The status code on a response head's first line, or 0. */
int httpStatus(const std::string &head);
/** A header's value, trimmed; "" when it is absent. Names are case-insensitive. */
std::string httpHeader(const std::string &head, const char *name);

/**
 * A WebSocket client (RFC 6455) over a Connection: text messages, pings,
 * pongs and the close handshake, which is all a Socket.IO client needs.
 * Single-threaded: call everything from the same task.
 */
class WebSocket {
  public:
    /** Most a client frame header takes: 2 bytes, an 8-byte length, a 4-byte mask. */
    static const size_t HEADROOM = 14;

    using TextHandler = std::function<void(const char *text, size_t len)>;

    /** Dial and upgrade. Blocks for the connection, the TLS handshake and the upgrade. */
    bool connect(const Url &url, const char *caRoots, int timeoutMs, std::string &error);
    /** Send a close frame if the connection still works, then drop it. */
    void close();
    bool connected() const { return _conn.isOpen() && !_failed; }

    /**
     * Read what has arrived and act on it: text messages go to `onText`
     * (NUL-terminated), pings are answered. False once the connection is
     * gone; call close() then.
     */
    bool poll(const TextHandler &onText);

    /**
     * Send a text message laid out at `buf + HEADROOM`, `len` bytes long. The
     * frame header goes into the headroom and the payload is masked in
     * place, so it leaves in one write and is never copied.
     */
    bool sendText(uint8_t *buf, size_t len);
    /** The same for a string, which is copied. */
    bool sendText(const char *text, size_t len);
    bool ping();

    /** Milliseconds since anything last arrived from the server. */
    uint32_t idleMs() const;

  private:
    bool sendFrame(uint8_t opcode, uint8_t *buf, size_t len);
    /** Act on every whole frame in _rx. False on a protocol error or a close. */
    bool parse(const TextHandler &onText);

    Connection _conn;
    /** Bytes read and not yet parsed. */
    std::string _rx;
    /** _rx holds bytes that came with the upgrade response, not parsed yet. */
    bool _unparsed = false;
    /** A message that came in fragments, so far. */
    std::string _message;
    uint8_t _messageOpcode = 0;
    bool _failed = false;
    uint32_t _lastRx = 0;
};

}  // namespace clavda

#endif
