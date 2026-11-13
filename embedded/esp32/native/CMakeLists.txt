cmake_minimum_required(VERSION 3.22)
get_filename_component(default_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(UG_ROOT "${default_root}" CACHE PATH "Universal GNSS source root")
set(UG_PROFILE "gateway" CACHE STRING "gateway, relay-ntrip or rover")
if(NOT UG_PROFILE MATCHES "^(gateway|relay-ntrip|rover)$")
    message(FATAL_ERROR "Unsupported ESP32 role")
endif()
set(COMPONENTS main)
set(EXTRA_COMPONENT_DIRS "${UG_ROOT}/embedded/esp32/components/ug_rtcm"
    "${UG_ROOT}/embedded/esp32/components/rtcm_filter"
    "${UG_ROOT}/embedded/esp32/components/lora_transport"
    "${UG_ROOT}/embedded/esp32/components/espnow_rtcm")
if(NOT UG_PROFILE STREQUAL "rover")
    list(APPEND EXTRA_COMPONENT_DIRS "${UG_ROOT}/embedded/esp32/components/ug_protocols"
        "${UG_ROOT}/embedded/esp32/components/ug_gateway")
    if(UG_LOCAL_GNSS)
        list(APPEND EXTRA_COMPONENT_DIRS "${UG_ROOT}/embedded/esp32/components/ug_receiver"
            "${UG_ROOT}/embedded/esp32/components/ug_receiver_runtime")
    endif()
endif()
set(PROJECT_VER "development")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(UG_PROFILE "${UG_PROFILE}")
idf_build_set_property(UG_LOCAL_GNSS "${UG_LOCAL_GNSS}")
project(universal-gnss-esp32)
