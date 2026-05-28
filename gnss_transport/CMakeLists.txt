add_library(
  universal_gnss_transport
  src/memory_stream.cpp
)

target_include_directories(
  universal_gnss_transport
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(universal_gnss_transport PUBLIC cxx_std_17)

if(BUILD_TESTING)
  add_executable(
    gnss_transport_test_foundation
    tests/test_transport_foundation.cpp
  )
  target_link_libraries(
    gnss_transport_test_foundation
    PRIVATE
      universal_gnss_transport
  )
  add_test(NAME gnss_transport_test_foundation COMMAND gnss_transport_test_foundation)
endif()
