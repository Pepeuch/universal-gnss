set(auth_sources)
set(auth_requires)
if(IDF_TARGET STREQUAL "esp32s3")
    list(APPEND auth_requires ug_network)
    list(APPEND auth_sources "src/ug_web_auth_platform.cpp")
    if(NOT CONFIG_UG_WEB_HTTP)
        list(APPEND auth_sources "src/ug_tls_bootstrap.cpp")
    endif()
    list(APPEND auth_requires bootloader_support)
endif()
idf_component_register(SRCS ${auth_sources} "src/ug_configuration.cpp" "src/ug_credentials.cpp" "src/ug_commission.cpp" "src/ug_https_diagnostics.cpp" "src/ug_memory_diagnostics.cpp" "src/ug_s3_json_memory.cpp" INCLUDE_DIRS "include"
    REQUIRES ${auth_requires} ug_supervisor ug_settings ug_pairing espnow_rtcm esp_hw_support nvs_flash mbedtls esp_timer esp_app_format)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

if(IDF_TARGET STREQUAL "esp32s3")
    # Pinned IDF 6.1 caller-owned SHA contexts; keep this driver header private
    # to the adapter. No SDK source, TLS configuration or driver code is changed.
    target_include_directories(${COMPONENT_LIB} PRIVATE
        "$ENV{IDF_PATH}/components/mbedtls/port/psa_driver/esp_sha/include")
endif()
