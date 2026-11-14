idf_component_register(SRCS "src/ug_gateway_service.cpp" INCLUDE_DIRS "include"
    REQUIRES ug_supervisor ug_gateway espnow_rtcm esp_timer)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
