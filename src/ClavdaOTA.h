/**
 *
 * @name ClavdaOTA
 * @author Clavda (support@clavda.com)
 * @brief Over-the-air firmware updates for the ESP32 family, from anywhere,
 *        through the Clavda relay.
 * @version 2.2.0
 * @date 2026-10-13
 *
 * The one header to include, from either language:
 *
 *  - C++ (a sketch, a .cpp file) gets the C++ API in cpp/: ClavdaOTA,
 *    ClavdaSerial, ClavdaWidget.
 *  - C (a .c file) gets the C API in c/clavda_ota.h: clavda_begin(),
 *    clavda_widget_create(), ... C++ can call those too.
 *
 * The clavda_device.h the console generates includes this, so it works from
 * either language as well.
 */

#ifndef CLAVDA_OTA_H
#define CLAVDA_OTA_H

#ifdef __cplusplus
  #include "cpp/ClavdaOTA.h"
#endif
#include "c/clavda_ota.h"

#endif
