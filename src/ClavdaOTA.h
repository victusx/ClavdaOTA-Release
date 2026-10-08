/**
 *
 * @name ClavdaOTA
 * @author Clavda (support@clavda.com)
 * @brief Over-the-air firmware updates for the ESP32 family, from anywhere,
 *        through the Clavda relay.
 * @version 2.1.0
 * @date 2026-10-07
 */

#ifndef ClavdaOTA_h
#define ClavdaOTA_h

#include <Arduino.h>
#include <functional>
#include "sdkconfig.h"
#include "ClavdaSerial.h"

#if !defined(ESP32)
  #error "ClavdaOTA 2.x runs on the ESP32 family only (ESP32, S2, S3, C3, C6)."
#endif

#define CLAVDA_LIB_VERSION "2.1.0"

/** The chip family as the IDF names it: "esp32", "esp32s3", "esp32c3", ... */
#define CLAVDA_CHIP CONFIG_IDF_TARGET

/**
 * Compiled into every image by CLAVDA_DEFINE_BUNDLE. When a .bin is uploaded,
 * the console finds this block by its magic and reads the version, the chip,
 * and whether the image carries a device's credentials, so nobody has to type
 * them in. Only fixed-size char arrays, so there is no padding and the layout
 * is identical on every target.
 *
 * The magic string must never appear anywhere else in the library: a second
 * copy in .rodata would look like a second descriptor.
 */
struct ClavdaDescriptor {
  char magic[16];     // CLAVDA_DESCRIPTOR_MAGIC
  char firmware[32];  // CLAVDA_FIRMWARE_VERSION
  char chip[16];      // CLAVDA_CHIP
  char library[16];   // CLAVDA_LIB_VERSION
  char bundle[64];    // device id the image carries; "" for a fleet image
};

#define CLAVDA_DESCRIPTOR_MAGIC "CLAVDA-FWDESC-1"

/** Everything the generated clavda_device.h hands to begin(). */
struct ClavdaBundle {
  const ClavdaDescriptor *descriptor;
  const char *relayUrl;
  const char *deviceId;   // "" in a fleet build
  const char *token;      // device JWT; "" in a fleet build
  const char *jobKey;     // base64 AES-256 key that seals OTA jobs; "" in a fleet build
  uint32_t generation;    // credential generation; 0 in a fleet build
  const char *caRoots;    // PEM roots for the relay and firmware download hosts
};

/**
 * Used by the generated clavda_device.h. The descriptor is defined in the
 * sketch's translation unit, where CLAVDA_FIRMWARE_VERSION is visible, and the
 * bundle points at it, which is what keeps the linker from dropping it.
 */
#define CLAVDA_DEFINE_BUNDLE(NAME, RELAY, DEVICE_ID, TOKEN, JOB_KEY, GENERATION, CA_ROOTS) \
  static const ClavdaDescriptor NAME##_DESCRIPTOR __attribute__((used)) = {             \
    CLAVDA_DESCRIPTOR_MAGIC, CLAVDA_FIRMWARE_VERSION, CLAVDA_CHIP, CLAVDA_LIB_VERSION,  \
    DEVICE_ID};                                                                          \
  static const ClavdaBundle NAME = {&NAME##_DESCRIPTOR, RELAY, DEVICE_ID, TOKEN,         \
                                    JOB_KEY, GENERATION, CA_ROOTS};

enum ClavdaStatus {
  /** No identity in NVS and none in the image: flash an image built with clavda_device.h. */
  CLAVDA_UNPROVISIONED = 0,
  /** Waiting for Wi-Fi, or dialling the relay. */
  CLAVDA_CONNECTING,
  CLAVDA_ONLINE,
  /** An update is downloading, verifying, or about to reboot into the new image. */
  CLAVDA_UPDATING,
  /** The relay refused the credentials. Retried every 15 minutes. */
  CLAVDA_REJECTED,
};

/** What onUpdateRequest() is asked to approve. */
struct ClavdaUpdateRequest {
  const char *jobId;
  const char *version;
  size_t size;
};

class ClavdaOTAClass {
  public:
    /**
     * Resolve the device identity (NVS first, then the bundle), and start
     * talking to the relay. Returns false when there is no identity at all.
     * Wi-Fi is the sketch's job; the library waits for it.
     */
    bool begin(const ClavdaBundle &bundle);
    void loop();

    /**
     * Veto an update, e.g. while a motor is running. Return false and the
     * console sees the job as deferred rather than failed.
     */
    void onUpdateRequest(std::function<bool(const ClavdaUpdateRequest &request)> callable);
    void onStart(std::function<void()> callable);
    void onProgress(std::function<void(size_t current, size_t final)> callable);
    void onEnd(std::function<void(bool success)> callable);

    ClavdaStatus status() const;
    /** Identity in use, from NVS. Empty until begin() succeeds. */
    const char *deviceId() const;
    const char *firmwareVersion() const;
    /** The running image carries another device's credentials; NVS kept its own. */
    bool bundleMismatch() const;
};

extern ClavdaOTAClass ClavdaOTA;
#endif
