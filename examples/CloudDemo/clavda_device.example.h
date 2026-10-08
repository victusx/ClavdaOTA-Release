// Placeholder so the CloudDemo example compiles out of the box. It carries no
// credentials, so a board flashed with it stays unprovisioned.
//
// Download the real clavda_device.h for your device from the Clavda console
// (Devices -> Manage) and put it next to the sketch; the example picks it up
// in place of this file. Never commit the real one: it contains secrets.

#pragma once

#include <ClavdaOTA.h>

#ifndef CLAVDA_FIRMWARE_VERSION
#error "Define CLAVDA_FIRMWARE_VERSION (for example \"1.0.0\") before including clavda_device.h"
#endif

CLAVDA_DEFINE_BUNDLE(CLAVDA_DEVICE, "https://server.clavda.com", "", "", "", 0, "")
