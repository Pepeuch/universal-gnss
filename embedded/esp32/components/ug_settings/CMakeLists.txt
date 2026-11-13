get_filename_component(UG_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
idf_component_register(SRCS "src/ug_settings.cpp" "src/ug_settings_store.cpp" "src/ug_admin.cpp" INCLUDE_DIRS "include"
    "${UG_ROOT}/core/gnss_ntrip/include" REQUIRES ug_protocols espressif__cjson)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
