idf_component_register(SRCS "src/ug_receiver_runtime.cpp" INCLUDE_DIRS "include" REQUIRES ug_receiver)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
