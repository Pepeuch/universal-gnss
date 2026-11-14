idf_component_register(SRCS "src/ug_configuration.cpp" "src/ug_credentials.cpp" "src/ug_commission.cpp" INCLUDE_DIRS "include"
    REQUIRES ug_supervisor ug_settings nvs_flash mbedtls)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
