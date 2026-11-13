idf_component_register(SRCS "src/ug_network_services.cpp" "src/ug_wifi.cpp" INCLUDE_DIRS "include"
    REQUIRES ug_platform ug_network ug_gateway_service esp_timer esp_wifi esp_netif espressif__mdns)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
if(CONFIG_IDF_TARGET_ESP32C3)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE UG_SERVICE_PUBLISHERS=1 UG_SERVICE_CASTER=0)
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE UG_SERVICE_PUBLISHERS=5 UG_SERVICE_CASTER=1)
endif()
