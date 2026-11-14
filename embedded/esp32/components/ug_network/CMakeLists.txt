get_filename_component(UG_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
file(READ "${UG_ROOT}/core/gnss_ntrip/CMakeLists.txt" declaration)
string(REGEX MATCH "add_library\\([^)]*\\)" declaration "${declaration}")
string(REGEX MATCHALL "src/[A-Za-z0-9_]+\\.cpp" sources "${declaration}")
# The socket caster backend is integrated separately, never replaced by a second client.
list(REMOVE_ITEM sources "src/ntrip_caster_orchestrator.cpp")
list(TRANSFORM sources PREPEND "${UG_ROOT}/core/gnss_ntrip/")
idf_component_register(SRCS "src/idf_tcp_transport.cpp" "src/ug_ntrip_connection.cpp" "src/ug_ntrip_publisher.cpp" ${sources}
    INCLUDE_DIRS "include" "${UG_ROOT}/core/gnss_ntrip/include" "${UG_ROOT}/core/gnss_transport/include"
    REQUIRES ug_protocols ug_gateway ug_rtcm ug_settings nvs_flash esp-tls mbedtls lwip esp_timer)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_definitions(${COMPONENT_LIB} PUBLIC UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT=1)
