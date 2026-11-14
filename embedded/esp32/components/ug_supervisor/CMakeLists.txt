idf_component_register(SRCS "src/ug_supervisor.cpp" INCLUDE_DIRS "include"
    REQUIRES esp_system esp_timer freertos)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
