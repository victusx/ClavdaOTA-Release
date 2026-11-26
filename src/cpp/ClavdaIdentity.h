#ifndef ClavdaIdentity_h
#define ClavdaIdentity_h

#include <stdint.h>
#include <string>
#include "../common/clavda_types.h"

namespace clavda {

/**
 * Who this board is, kept in NVS. NVS is its own partition, so neither an app
 * OTA nor a filesystem update touches it: the identity outlives every image.
 * The keys are the ones the library has always used, so a board keeps its
 * identity whichever toolchain built the image it runs.
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

    const std::string &deviceId() const { return _deviceId; }
    const std::string &token() const { return _token; }
    const std::string &jobKey() const { return _jobKey; }
    uint32_t generation() const { return _generation; }
    /** The image carries a bundle for a different device; NVS kept its own. */
    bool mismatch() const { return _mismatch; }

    /** Last job that ran to completion, so a replayed job is refused. */
    std::string lastJobId();
    /** Recorded just before rebooting into a new image, read back once on the next boot. */
    void setPendingJob(const std::string &jobId, const std::string &targetVersion);
    bool takePendingJob(std::string &jobId, std::string &targetVersion);

  private:
    std::string _deviceId;
    std::string _token;
    std::string _jobKey;
    uint32_t _generation = 0;
    bool _mismatch = false;
};

}  // namespace clavda

#endif
