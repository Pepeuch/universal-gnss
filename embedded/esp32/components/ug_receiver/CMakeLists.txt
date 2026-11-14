get_filename_component(UG_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
file(READ "${UG_ROOT}/core/gnss_driver/CMakeLists.txt" declaration)
string(REGEX MATCH "add_library\\([^)]*\\)" declaration "${declaration}")
string(REGEX MATCHALL "src/[A-Za-z0-9_]+\\.cpp" sources "${declaration}")
# IDF uses the same byte-probe/model-query/config planners; host /dev enumeration
# and POSIX opening are compiled out in the owning implementation.
list(TRANSFORM sources PREPEND "${UG_ROOT}/core/gnss_driver/")
idf_component_register(SRCS ${sources}
    "${UG_ROOT}/core/gnss_transport/src/rtcm_frame_writer.cpp"
    INCLUDE_DIRS "${UG_ROOT}/core/gnss_driver/include" "${UG_ROOT}/core/gnss_transport/include"
    REQUIRES ug_protocols)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
