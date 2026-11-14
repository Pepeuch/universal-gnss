idf_component_register(SRCS "src/ug_gateway.cpp" INCLUDE_DIRS "include" REQUIRES ug_protocols)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
# Capacity specialization only: the C3 relay supports radio + one uploader.
if(CONFIG_IDF_TARGET_ESP32C3)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC UG_GATEWAY_SINKS=2)
endif()
