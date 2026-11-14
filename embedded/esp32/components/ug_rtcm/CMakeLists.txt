get_filename_component(UG_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
idf_component_register(
  SRCS "src/rtcm.cpp" "${UG_ROOT}/protocols/gnss_protocols/src/rtcm_crc24q.cpp"
  INCLUDE_DIRS "include"
  PRIV_INCLUDE_DIRS "${UG_ROOT}/protocols/gnss_protocols/include"
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
