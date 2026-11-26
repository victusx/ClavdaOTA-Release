#ifndef ClavdaSerial_h
#define ClavdaSerial_h

#include <functional>
#include <string>
#include "freertos/FreeRTOS.h"
#include "ClavdaPrint.h"

/**
 * Output kept for monitors that open late, or after a reboot - about 30 lines
 * of typical logging. The only RAM ClavdaSerial holds; set per begin().
 */
#ifndef CLAVDA_SERIAL_BUFFER_SIZE
  #define CLAVDA_SERIAL_BUFFER_SIZE 2048
#endif

namespace clavda {
class Monitor;
}

/**
 * A Serial you can watch from the Clavda console. Print to it as you would to
 * Serial: everything printed is streamed, end-to-end encrypted, to each Serial
 * Monitor open on this device, and lines typed there arrive in onMessage().
 *
 * The cloud counterpart of WebSerial - nothing listens on the device; output
 * travels over the same outbound relay connection as updates.
 *
 * Output goes into a ring buffer, so a monitor that opens late, or reopens
 * after the device rebooted, starts with the most recent output. When nobody
 * is watching, or a monitor cannot keep up, the oldest output is overwritten
 * and the console says how much it missed.
 *
 * Safe to print to from any task.
 */
class ClavdaSerialClass : public ClavdaPrint {
  public:
    using MessageHandler = std::function<void(uint8_t *data, size_t len)>;
#if CLAVDA_ARDUINO
    using StringMessageHandler = std::function<void(const String &msg)>;

    /**
     * Allocate the buffer. `mirror` - usually &Serial - also gets everything
     * printed, so replacing Serial with ClavdaSerial keeps the USB output.
     * Returns false if the buffer could not be allocated.
     */
    bool begin(Print *mirror = nullptr, size_t bufferSize = CLAVDA_SERIAL_BUFFER_SIZE);
#else
    using StringMessageHandler = std::function<void(const std::string &msg)>;

    /**
     * Allocate the buffer. With `mirror`, everything printed also goes to
     * stdout - the USB or UART console - so a cable still shows it. Returns
     * false if the buffer could not be allocated.
     */
    bool begin(bool mirror = true, size_t bufferSize = CLAVDA_SERIAL_BUFFER_SIZE);
#endif

    /**
     * Also send ESP-IDF's log output (ESP_LOGx, and the system's own messages)
     * to the console's Serial Monitor; it still goes to the USB or UART
     * console as before. Off again with captureLogs(false).
     */
    void captureLogs(bool on = true);

    /**
     * A line typed in a console Serial Monitor, without a line ending. The
     * data is NUL-terminated (data[len] is 0), which the C API relies on.
     */
    void onMessage(MessageHandler handler);
    void onMessage(StringMessageHandler handler);

    /** Serial Monitors open on this device right now. */
    size_t monitors() const { return _monitors; }

    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    using ClavdaPrint::write;

  private:
    friend class clavda::Monitor;

    /** Installed by captureLogs(): ESP-IDF's vprintf, teed into the buffer. */
    static int logHook(const char *format, va_list args);
    /** Into the ring buffer only, not the mirror. */
    void record(const uint8_t *buffer, size_t size);
    /** Offset of the oldest byte still buffered, and of the next one to be written. */
    void window(uint64_t &oldest, uint64_t &head);
    /**
     * Copy up to `cap` bytes from offset `from` into `out`. A `from` that was
     * already overwritten is moved forward to the oldest byte still held.
     */
    size_t read(uint64_t &from, uint8_t *out, size_t cap);
    void deliver(uint8_t *data, size_t len);

    uint8_t *_buf = nullptr;
    size_t _cap = 0;
    /** Bytes ever written; the buffer holds the last min(_head, _cap) of them. */
    uint64_t _head = 0;
#if CLAVDA_ARDUINO
    Print *_mirror = nullptr;
#else
    bool _mirror = false;
#endif
    MessageHandler _onMessage;
    StringMessageHandler _onString;
    size_t _monitors = 0;
    portMUX_TYPE _lock = portMUX_INITIALIZER_UNLOCKED;
};

extern ClavdaSerialClass ClavdaSerial;

#endif
