idf_component_register(SRCS "src/ug_gateway_service.cpp" INCLUDE_DIRS "include"
    REQUIRES ug_supervisor ug_gateway espnow_rtcm esp_timer)
if(CONFIG_IDF_TARGET_ESP32S3)
    idf_component_optional_requires(PRIVATE heap esp_hw_support)
endif()
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
