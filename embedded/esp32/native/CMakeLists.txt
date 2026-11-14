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
        "${UG_ROOT}/embedded/esp32/components/ug_gateway"
        "${UG_ROOT}/embedded/esp32/components/ug_network"
        "${UG_ROOT}/embedded/esp32/components/ug_services"
        "${UG_ROOT}/embedded/esp32/components/ug_supervisor"
        "${UG_ROOT}/embedded/esp32/components/ug_selftest"
        "${UG_ROOT}/embedded/esp32/components/ug_web"
        "${UG_ROOT}/embedded/esp32/components/ug_settings"
        "${UG_ROOT}/embedded/esp32/components/ug_platform"
        "${UG_ROOT}/embedded/esp32/components/ug_gateway_service"
        "${UG_ROOT}/embedded/esp32/components/ug_ethernet")
    if(UG_LOCAL_GNSS)
        list(APPEND EXTRA_COMPONENT_DIRS "${UG_ROOT}/embedded/esp32/components/ug_receiver"
            "${UG_ROOT}/embedded/esp32/components/ug_receiver_runtime"
            "${UG_ROOT}/embedded/esp32/components/ug_gnss")
    endif()
endif()
set(PROJECT_VER "development")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(UG_PROFILE "${UG_PROFILE}")
idf_build_set_property(UG_LOCAL_GNSS "${UG_LOCAL_GNSS}")
project(universal-gnss-esp32)

# IDF 6.1 defaults to GNU C23/C++26. UG-owned sources intentionally retain their
# portable C17/C++17 contract; leave SDK/managed components on their own defaults.
idf_build_get_property(native_components BUILD_COMPONENTS)
foreach(component IN LISTS native_components)
    idf_component_get_property(component_dir ${component} COMPONENT_DIR)
    string(FIND "${component_dir}" "${UG_ROOT}/embedded/esp32/components/" owned_prefix)
    if(component STREQUAL "main" OR owned_prefix EQUAL 0)
        idf_component_get_property(component_lib ${component} COMPONENT_LIB)
        target_compile_options(${component_lib} PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:-std=gnu17>"
            "$<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>")
        # Historical wire helpers logged every successful frame/fragment. Keep
        # warnings/errors without console work in the ordinary RTCM hot path.
        # The ESP-NOW runtime's startup/channel messages remain independent.
        if(NOT UG_PROFILE STREQUAL "rover" AND CONFIG_LOG_MAXIMUM_LEVEL GREATER 2 AND
           (component STREQUAL "lora_transport" OR component STREQUAL "rtcm_filter"))
            target_compile_definitions(${component_lib} PRIVATE LOG_LOCAL_LEVEL=ESP_LOG_WARN)
        endif()
    endif()
endforeach()
