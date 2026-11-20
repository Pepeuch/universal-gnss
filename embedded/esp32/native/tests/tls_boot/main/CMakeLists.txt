idf_component_register(SRCS "main.cpp"
    "${UG_ROOT}/embedded/esp32/components/ug_platform/src/ug_credentials.cpp"
    "${UG_ROOT}/embedded/esp32/components/ug_platform/src/ug_tls_bootstrap.cpp"
    "${UG_ROOT}/embedded/esp32/components/ug_network/src/ug_network_allocation.cpp"
    "${UG_ROOT}/embedded/esp32/components/ug_pairing/src/ug_pairing_pmk.c"
    INCLUDE_DIRS "${UG_ROOT}/embedded/esp32/components/ug_platform/include"
                 "${UG_ROOT}/embedded/esp32/components/ug_settings/include"
                 "${UG_ROOT}/embedded/esp32/components/ug_network/include"
                 "${UG_ROOT}/embedded/esp32/components/ug_pairing/include"
    REQUIRES ug_protocols mbedtls nvs_flash bootloader_support esp_timer esp_hw_support heap esp_psram espressif__cjson)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_definitions(${COMPONENT_LIB} PRIVATE UG_NETWORK_ALLOCATION_TEST=1)
target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
    $<$<COMPILE_LANGUAGE:C>:-std=gnu17> -UNDEBUG)
target_link_options(${COMPONENT_LIB} INTERFACE -Wl,--wrap=heap_caps_calloc -Wl,--wrap=nvs_set_blob -Wl,--wrap=nvs_commit)
