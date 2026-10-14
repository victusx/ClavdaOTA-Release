#ifndef ClavdaIdentity_h
#define ClavdaIdentity_h

#include <Arduino.h>
#include "ClavdaOTA.h"

namespace clavda {

/**
 * Who this board is, kept in NVS. NVS is its own partition, so neither an app
 * OTA nor a filesystem update touches it: the identity outlives every image.
 *
 * Seed once, then NVS wins. The image's bundle only ever fills an empty NVS, or
 * rotates the same device's credentials to a newer generation. An image built
 * for another device never replaces this one - so the same fleet image can go
 * to every board, and an image cannot take over a board. The one way to give a
 * board a new identity is to erase its flash.
 */
class Identity {
  public:
    /** Apply the seed-once rules. False when there is no usable identity at all. */
    bool resolve(const ClavdaBundle &bundle);

    const String &deviceId() const { return _deviceId; }
    const String &token() const { return _token; }
    const String &jobKey() const { return _jobKey; }
    uint32_t generation() const { return _generation; }
    /** The image carries a bundle for a different device; NVS kept its own. */
    bool mismatch() const { return _mismatch; }

    /** Last job that ran to completion, so a replayed job is refused. */
    String lastJobId();
    /** Recorded just before rebooting into a new image, read back once on the next boot. */
    void setPendingJob(const String &jobId, const String &targetVersion);
    bool takePendingJob(String &jobId, String &targetVersion);

  private:
    String _deviceId;
    String _token;
    String _jobKey;
    uint32_t _generation = 0;
    bool _mismatch = false;
};

}  // namespace clavda

#endif
