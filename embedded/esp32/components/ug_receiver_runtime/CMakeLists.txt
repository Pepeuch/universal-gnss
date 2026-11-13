idf_component_register(SRCS "src/ug_receiver_runtime.cpp" "src/ug_base_operation.cpp" "src/ug_telemetry_operation.cpp" INCLUDE_DIRS "include" REQUIRES ug_receiver ug_settings)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
