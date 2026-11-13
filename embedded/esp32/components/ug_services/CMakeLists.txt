idf_component_register(SRCS "src/ug_network_services.cpp" "src/ug_wifi.cpp" INCLUDE_DIRS "include"
    REQUIRES ug_platform ug_network ug_gateway_service esp_timer esp_wifi esp_netif espressif__mdns)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
