add_library(
  universal_gnss_ntrip
  src/ntrip_request.cpp
)

target_include_directories(
  universal_gnss_ntrip
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(universal_gnss_ntrip PUBLIC cxx_std_17)

if(BUILD_TESTING)
  add_executable(
    gnss_ntrip_test_foundation
    tests/test_ntrip_foundation.cpp
  )
  target_link_libraries(
    gnss_ntrip_test_foundation
    PRIVATE
      universal_gnss_ntrip
  )
  add_test(NAME gnss_ntrip_test_foundation COMMAND gnss_ntrip_test_foundation)
endif()
