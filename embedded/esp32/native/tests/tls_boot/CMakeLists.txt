cmake_minimum_required(VERSION 3.22)
# Dedicated SDK/QEMU test image, never an application/catalogue variant.
set(EXTRA_COMPONENT_DIRS "${UG_CJSON_COMPONENT}")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ug-tls-boot-test)
