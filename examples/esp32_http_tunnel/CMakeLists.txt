# xfrpc-esp32 HTTP tunnel example — standalone ESP-IDF project.
#
# Build from this directory:
#     idf.py set-target esp32s3
#     idf.py menuconfig      # WiFi SSID/password, frps address
#     idf.py build flash monitor
#
# The xfrpc component lives in the repository root's components/ directory,
# which is added below via EXTRA_COMPONENT_DIRS.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(xfrpc_http_tunnel)