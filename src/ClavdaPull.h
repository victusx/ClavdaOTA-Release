#ifndef ClavdaPull_h
#define ClavdaPull_h

#include <Arduino.h>
#include <functional>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include "ClavdaCrypto.h"

namespace clavda {

/**
 * Streams a firmware image from its presigned URL into the inactive OTA slot.
 * The body is written a slice at a time from loop() - never inside a relay
 * event handler - so the relay link keeps answering pings throughout.
 *
 * Nothing is committed until the whole image hashes to the job's SHA-256: a
 * mismatch aborts the flash region and the running image stays as it was.
 */
class Pull {
  public:
    enum Phase { IDLE, CONNECTING, DOWNLOADING, VERIFYING, DONE, FAILED };

    struct Job {
      String jobId;
      String url;
      String sha256;
      String version;
      size_t size = 0;
    };

    ~Pull();

    /** Arm a download. The connection is opened on the next loop(). */
    bool start(const Job &job, const char *caRoots);
    void loop();
    void abort(const char *why);
    /** Forget a finished (DONE or FAILED) run. */
    void reset();

    bool busy() const { return _phase == CONNECTING || _phase == DOWNLOADING || _phase == VERIFYING; }
    Phase phase() const { return _phase; }
    const Job &job() const { return _job; }
    size_t written() const { return _written; }
    const String &error() const { return _error; }

    void onProgress(std::function<void(size_t current, size_t total)> cb) { _onProgress = cb; }

  private:
    void open();
    void stream();
    void verify();
    void fail(const String &why);
    void release();

    Phase _phase = IDLE;
    Job _job;
    const char *_caRoots = nullptr;
    size_t _written = 0;
    uint32_t _lastData = 0;
    bool _flashOpen = false;
    String _error;
    Sha256 _sha;

    HTTPClient *_http = nullptr;
    WiFiClient *_client = nullptr;
    std::function<void(size_t, size_t)> _onProgress;
};

}  // namespace clavda

#endif
