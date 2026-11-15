idf_component_register(SRCS "src/ug_configuration.cpp" "src/ug_credentials.cpp" "src/ug_commission.cpp" "src/ug_https_diagnostics.cpp" "src/ug_memory_diagnostics.cpp" "src/ug_s3_json_memory.cpp" INCLUDE_DIRS "include"
    REQUIRES ug_supervisor ug_settings nvs_flash mbedtls esp_timer esp_app_format)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
