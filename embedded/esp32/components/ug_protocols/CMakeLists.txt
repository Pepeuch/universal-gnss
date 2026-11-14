get_filename_component(UG_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
# Read the owning library's production source list; never compile its host tests.
file(READ "${UG_ROOT}/protocols/gnss_protocols/CMakeLists.txt" declaration)
string(REGEX MATCH "add_library\\([^)]*\\)" declaration "${declaration}")
string(REGEX MATCHALL "src/[A-Za-z0-9_]+\\.cpp" sources "${declaration}")
list(REMOVE_ITEM sources "src/rtcm_crc24q.cpp") # Provided once by ug_rtcm.
list(TRANSFORM sources PREPEND "${UG_ROOT}/protocols/gnss_protocols/")
idf_component_register(SRCS ${sources}
    INCLUDE_DIRS "${UG_ROOT}/protocols/gnss_protocols/include" "${UG_ROOT}/core/gnss_core/include"
    REQUIRES ug_rtcm)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
