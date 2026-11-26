# ClavdaOTA as an ESP-IDF component (ESP-IDF 4.4 and later).
#
# The Arduino IDE and PlatformIO ignore this file: they build src/ from
# library.properties and library.json. All three build the same sources.

file(GLOB_RECURSE CLAVDA_SOURCES "${CMAKE_CURRENT_LIST_DIR}/src/*.cpp")

idf_component_register(
  SRCS ${CLAVDA_SOURCES}
  INCLUDE_DIRS "src"
  REQUIRES freertos
  PRIV_REQUIRES app_update esp-tls esp_event esp_netif esp_timer esp_wifi lwip mbedtls nvs_flash
                spi_flash tcp_transport
)

# The plain ESP-IDF build of the API, for this component and everything that
# uses it, even with the Arduino component in the same project.
target_compile_definitions(${COMPONENT_LIB} PUBLIC CLAVDA_ARDUINO=0)
