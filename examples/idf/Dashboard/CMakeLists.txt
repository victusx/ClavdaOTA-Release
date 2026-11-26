# ClavdaOTA Dashboard Demo for ESP-IDF. Build it like any ESP-IDF project:
#
#   idf.py set-target esp32s3
#   idf.py build flash monitor

cmake_minimum_required(VERSION 3.16)

# This copy of the library: the root of the repository the example is in. In a
# project of your own, add it to main/idf_component.yml instead:
#
#   dependencies:
#     clavdaota:
#       git: https://github.com/victusx/ClavdaOTA-Release.git
#
# or clone it into the project's components/ folder.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../..")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(clavda_dashboard)
