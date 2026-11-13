idf_build_get_property(UG_LOCAL_GNSS UG_LOCAL_GNSS)
set(deps ug_platform ug_services ug_gateway_service esp_https_server esp_timer)
if(UG_LOCAL_GNSS)
    list(APPEND deps ug_gnss)
endif()
idf_component_register(SRCS "src/ug_web.cpp" "src/ug_https_defaults.c" INCLUDE_DIRS "include" REQUIRES ug_supervisor ${deps}
    EMBED_TXTFILES "assets/index.html" "assets/app.js" "assets/style.css")
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_definitions(${COMPONENT_LIB} PRIVATE UG_LOCAL_GNSS=$<BOOL:${UG_LOCAL_GNSS}>)
