/**
 * Types and macros the C and the C++ API share. Plain C, so a .c file can use
 * them, and so can the clavda_device.h the console generates, whichever
 * language includes it.
 */

#ifndef CLAVDA_TYPES_H
#define CLAVDA_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include "sdkconfig.h"

#if !defined(ESP32) && !defined(ESP_PLATFORM)
  #error "ClavdaOTA 2.x runs on the ESP32 family only (ESP32, S2, S3, C3, C6)."
#endif

#define CLAVDA_LIB_VERSION "2.2.0"

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
typedef struct ClavdaDescriptor {
  char magic[16];     // CLAVDA_DESCRIPTOR_MAGIC
  char firmware[32];  // CLAVDA_FIRMWARE_VERSION
  char chip[16];      // CLAVDA_CHIP
  char library[16];   // CLAVDA_LIB_VERSION
  char bundle[64];    // device id the image carries; "" for a fleet image
} ClavdaDescriptor;

#define CLAVDA_DESCRIPTOR_MAGIC "CLAVDA-FWDESC-1"

/** Everything the generated clavda_device.h hands to begin(). */
typedef struct ClavdaBundle {
  const ClavdaDescriptor *descriptor;
  const char *relayUrl;
  const char *deviceId;  // "" in a fleet build
  const char *token;     // device JWT; "" in a fleet build
  const char *jobKey;    // base64 AES-256 key that seals OTA jobs; "" in a fleet build
  uint32_t generation;   // credential generation; 0 in a fleet build
  const char *caRoots;   // PEM roots for the relay and firmware download hosts
} ClavdaBundle;

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

typedef enum ClavdaStatus {
  /** No identity in NVS and none in the image: flash an image built with clavda_device.h. */
  CLAVDA_UNPROVISIONED = 0,
  /** Waiting for Wi-Fi, or dialling the relay. */
  CLAVDA_CONNECTING,
  CLAVDA_ONLINE,
  /** An update is downloading, verifying, or about to reboot into the new image. */
  CLAVDA_UPDATING,
  /** The relay refused the credentials. Retried every 15 minutes. */
  CLAVDA_REJECTED,
} ClavdaStatus;

/** What onUpdateRequest() is asked to approve. */
typedef struct ClavdaUpdateRequest {
  const char *jobId;
  const char *version;
  size_t size;
} ClavdaUpdateRequest;

/** How a status card colours its message: set("Door open", CLAVDA_WARNING). */
typedef enum ClavdaLevel {
  CLAVDA_NEUTRAL = 0,
  CLAVDA_INFO,
  CLAVDA_SUCCESS,
  CLAVDA_WARNING,
  CLAVDA_DANGER,
} ClavdaLevel;

#endif
