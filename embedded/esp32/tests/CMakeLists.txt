cmake_minimum_required(VERSION 3.22)
project(ug_embedded_tests LANGUAGES C CXX)
enable_testing()
get_filename_component(UG_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
add_library(ug_rtcm ../components/ug_rtcm/src/rtcm.cpp
  "${UG_ROOT}/protocols/gnss_protocols/src/rtcm_crc24q.cpp")
target_include_directories(ug_rtcm PUBLIC ../components/ug_rtcm/include
  PRIVATE "${UG_ROOT}/protocols/gnss_protocols/include")
target_compile_features(ug_rtcm PUBLIC cxx_std_17)
add_executable(test_rtcm test_rtcm.c)
target_compile_options(test_rtcm PRIVATE -UNDEBUG)
target_link_libraries(test_rtcm PRIVATE ug_rtcm)
add_test(NAME embedded_rtcm_c_api COMMAND test_rtcm)
