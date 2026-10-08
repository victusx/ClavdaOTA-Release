#ifndef ClavdaSerial_h
#define ClavdaSerial_h

#include <Arduino.h>
#include <functional>
#include "freertos/FreeRTOS.h"

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
class ClavdaSerialClass : public Print {
  public:
    using MessageHandler = std::function<void(uint8_t *data, size_t len)>;
    using StringMessageHandler = std::function<void(const String &msg)>;

    /**
     * Allocate the buffer. `mirror` - usually &Serial - also gets everything
     * printed, so replacing Serial with ClavdaSerial keeps the USB output.
     * Returns false if the buffer could not be allocated.
     */
    bool begin(Print *mirror = nullptr, size_t bufferSize = CLAVDA_SERIAL_BUFFER_SIZE);

    /** A line typed in a console Serial Monitor, without a line ending. */
    void onMessage(MessageHandler handler);
    void onMessage(StringMessageHandler handler);

    /** Serial Monitors open on this device right now. */
    size_t monitors() const { return _monitors; }

    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    using Print::write;

  private:
    friend class clavda::Monitor;

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
    Print *_mirror = nullptr;
    MessageHandler _onMessage;
    StringMessageHandler _onString;
    size_t _monitors = 0;
    portMUX_TYPE _lock = portMUX_INITIALIZER_UNLOCKED;
};

extern ClavdaSerialClass ClavdaSerial;

#endif
