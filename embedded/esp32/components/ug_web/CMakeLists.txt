idf_build_get_property(UG_LOCAL_GNSS UG_LOCAL_GNSS)
set(deps ug_platform ug_services ug_gateway_service ug_selftest esp_https_server esp_timer)
if(UG_LOCAL_GNSS)
    list(APPEND deps ug_gnss)
endif()
idf_component_register(SRCS "src/ug_web.cpp" "src/ug_https_defaults.c" INCLUDE_DIRS "include" REQUIRES ug_supervisor ${deps})
set(asset_output "${CMAKE_CURRENT_BINARY_DIR}/web_assets")
set(asset_sources "${CMAKE_CURRENT_LIST_DIR}/assets/index.html" "${CMAKE_CURRENT_LIST_DIR}/assets/app.js" "${CMAKE_CURRENT_LIST_DIR}/assets/style.css")
set(asset_files "${asset_output}/index.html.gz" "${asset_output}/app.js.gz" "${asset_output}/style.css.gz")
add_custom_command(OUTPUT ${asset_files}
    COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/tools/pack_assets.py"
        --source "${CMAKE_CURRENT_LIST_DIR}/assets" --output "${asset_output}"
    DEPENDS ${asset_sources} "${CMAKE_CURRENT_LIST_DIR}/tools/pack_assets.py"
    VERBATIM)
target_add_binary_data(${COMPONENT_LIB} "${asset_output}/index.html.gz" BINARY RENAME_TO ug_web_index_gz)
target_add_binary_data(${COMPONENT_LIB} "${asset_output}/app.js.gz" BINARY RENAME_TO ug_web_js_gz)
target_add_binary_data(${COMPONENT_LIB} "${asset_output}/style.css.gz" BINARY RENAME_TO ug_web_css_gz)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_definitions(${COMPONENT_LIB} PRIVATE UG_LOCAL_GNSS=$<BOOL:${UG_LOCAL_GNSS}>)
