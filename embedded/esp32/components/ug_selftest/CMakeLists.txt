idf_component_register(SRCS "src/ug_wire_selftest.cpp" INCLUDE_DIRS "include"
    REQUIRES espnow_rtcm ug_rtcm ug_settings)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
