/**
 *
 * @name ClavdaOTA
 * @author Clavda (support@clavda.com)
 * @brief Over-the-air firmware updates for the ESP32 family, from anywhere,
 *        through the Clavda relay.
 * @version 2.2.0
 * @date 2026-10-13
 */

#ifndef ClavdaOTA_h
#define ClavdaOTA_h

#include <Arduino.h>
#include <functional>
#include "../common/clavda_types.h"
#include "ClavdaSerial.h"
#include "ClavdaWidget.h"

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
