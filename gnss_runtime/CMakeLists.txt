find_package(Threads REQUIRED)

add_library(universal_gnss_runtime src/receiver_supervisor.cpp)
target_include_directories(universal_gnss_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(
  universal_gnss_runtime
  PUBLIC universal_gnss_driver universal_gnss_ntrip universal_gnss_transport Threads::Threads
)
target_compile_features(universal_gnss_runtime PUBLIC cxx_std_17)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_sources(universal_gnss_runtime PRIVATE src/posix_serial_factory.cpp)
  add_executable(universal_gnss_supervisor src/universal_gnss_supervisor.cpp)
  target_link_libraries(universal_gnss_supervisor PRIVATE universal_gnss_runtime)
endif()

install(TARGETS universal_gnss_runtime ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  install(TARGETS universal_gnss_supervisor RUNTIME DESTINATION bin)
endif()
install(DIRECTORY include/ DESTINATION include)

if(BUILD_TESTING)
  add_executable(gnss_runtime_test_receiver_supervisor tests/test_receiver_supervisor.cpp)
  target_link_libraries(gnss_runtime_test_receiver_supervisor PRIVATE universal_gnss_runtime)
  add_test(NAME gnss_runtime_test_receiver_supervisor COMMAND gnss_runtime_test_receiver_supervisor)
endif()
