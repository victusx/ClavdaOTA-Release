#ifndef ClavdaMbedtlsConfig_h
#define ClavdaMbedtlsConfig_h

/**
 * ESP-IDF builds mbedTLS with its own configuration, mbedtls/esp_config.h:
 * hardware AES, GCM and SHA, with context types of their own. Its mbedtls
 * component passes -DMBEDTLS_CONFIG_FILE to everything that requires it, and
 * the Arduino core passes it to every sketch and library. PlatformIO's ESP-IDF
 * builder compiles lib_deps libraries without it, and there the mbedTLS
 * headers describe plain mbedTLS instead: the GCM calls then fail to link, and
 * the SHA contexts would have the wrong size. Naming the same file here, before
 * any mbedTLS header, gives every build of the library the configuration
 * ESP-IDF itself was built with; where the define is already set, this does
 * nothing.
 *
 * ESP-IDF 6 (mbedTLS 4) also passes TF-PSA-Crypto's config files, which no
 * PlatformIO platform ships yet; this covers ESP-IDF 4.4 to 5.x.
 *
 * Internal: include it first in any header that leads to mbedTLS.
 */
#if defined(ESP_PLATFORM) && !defined(MBEDTLS_CONFIG_FILE)
  #define MBEDTLS_CONFIG_FILE "mbedtls/esp_config.h"
#endif

#endif
