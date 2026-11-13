idf_component_register(SRCS "src/ug_gnss_service.cpp" INCLUDE_DIRS "include"
    REQUIRES ug_receiver_runtime ug_gateway_service ug_platform ug_services esp_driver_uart esp_timer)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
