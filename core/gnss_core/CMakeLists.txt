add_library(universal_gnss_core INTERFACE)

target_include_directories(
  universal_gnss_core
  INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(universal_gnss_core INTERFACE cxx_std_17)

if(BUILD_TESTING)
  add_executable(gnss_core_test_runtime_state tests/test_runtime_state.cpp)
  target_link_libraries(gnss_core_test_runtime_state PRIVATE universal_gnss_core)
  add_test(NAME gnss_core_test_runtime_state COMMAND gnss_core_test_runtime_state)

  add_executable(gnss_core_test_runtime_aggregator tests/test_runtime_aggregator.cpp)
  target_link_libraries(gnss_core_test_runtime_aggregator PRIVATE universal_gnss_core)
  add_test(NAME gnss_core_test_runtime_aggregator COMMAND gnss_core_test_runtime_aggregator)

  add_executable(gnss_core_test_diagnostics tests/test_diagnostics.cpp)
  target_link_libraries(gnss_core_test_diagnostics PRIVATE universal_gnss_core)
  add_test(NAME gnss_core_test_diagnostics COMMAND gnss_core_test_diagnostics)
endif()
