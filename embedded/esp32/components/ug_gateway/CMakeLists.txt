idf_component_register(SRCS "src/ug_gateway.cpp" INCLUDE_DIRS "include" REQUIRES ug_protocols)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
