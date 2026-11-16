idf_component_register(
    SRCS
        "src/rtcm_filter.c"
        "src/ug_rtcm_rules.cpp"
        "src/rtcm_profiles.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_timer
        ug_rtcm
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
