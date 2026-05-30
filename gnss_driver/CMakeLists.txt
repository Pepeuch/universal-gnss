add_library(
  universal_gnss_driver
  src/receiver_command_dispatcher.cpp
  src/receiver_command_transaction_engine.cpp
  src/receiver_profiles.cpp
  src/receiver_session.cpp
  src/receiver_session_runner.cpp
  src/stream_detector.cpp
  src/ubx_command_response_mapper.cpp
  src/ublox_config_profile_builder.cpp
  src/ublox_session.cpp
  src/unicore_config_profile_builder.cpp
  src/unicore_session.cpp
)

target_include_directories(
  universal_gnss_driver
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
  universal_gnss_driver
  PUBLIC
    universal_gnss_protocols
    universal_gnss_transport
)

target_compile_features(universal_gnss_driver PUBLIC cxx_std_17)

if(BUILD_TESTING)
  add_executable(
    gnss_driver_test_foundation
    tests/test_driver_foundation.cpp
  )
  target_link_libraries(
    gnss_driver_test_foundation
    PRIVATE
      universal_gnss_driver
  )
  add_test(NAME gnss_driver_test_foundation COMMAND gnss_driver_test_foundation)

  add_executable(
    gnss_driver_test_unicore_session
    tests/test_unicore_session.cpp
  )
  target_link_libraries(
    gnss_driver_test_unicore_session
    PRIVATE
      universal_gnss_driver
  )
  add_test(NAME gnss_driver_test_unicore_session COMMAND gnss_driver_test_unicore_session)

  add_executable(
    gnss_driver_test_ublox_session
    tests/test_ublox_session.cpp
  )
  target_link_libraries(
    gnss_driver_test_ublox_session
    PRIVATE
      universal_gnss_driver
  )
  add_test(NAME gnss_driver_test_ublox_session COMMAND gnss_driver_test_ublox_session)

  add_executable(
    gnss_driver_test_receiver_session
    tests/test_receiver_session.cpp
  )
  target_link_libraries(
    gnss_driver_test_receiver_session
    PRIVATE
      universal_gnss_driver
  )
  add_test(NAME gnss_driver_test_receiver_session COMMAND gnss_driver_test_receiver_session)

  add_executable(
    gnss_driver_test_receiver_session_runner
    tests/test_receiver_session_runner.cpp
  )
  target_link_libraries(
    gnss_driver_test_receiver_session_runner
    PRIVATE
      universal_gnss_driver
  )
  add_test(
    NAME gnss_driver_test_receiver_session_runner
    COMMAND gnss_driver_test_receiver_session_runner
  )

  add_executable(
    gnss_driver_test_receiver_command_dispatcher
    tests/test_receiver_command_dispatcher.cpp
  )
  target_link_libraries(
    gnss_driver_test_receiver_command_dispatcher
    PRIVATE
      universal_gnss_driver
  )
  add_test(
    NAME gnss_driver_test_receiver_command_dispatcher
    COMMAND gnss_driver_test_receiver_command_dispatcher
  )

  add_executable(
    gnss_driver_test_receiver_command_model
    tests/test_receiver_command_model.cpp
  )
  target_link_libraries(
    gnss_driver_test_receiver_command_model
    PRIVATE
      universal_gnss_driver
  )
  add_test(
    NAME gnss_driver_test_receiver_command_model
    COMMAND gnss_driver_test_receiver_command_model
  )

  add_executable(
    gnss_driver_test_receiver_command_transaction
    tests/test_receiver_command_transaction.cpp
  )
  target_link_libraries(
    gnss_driver_test_receiver_command_transaction
    PRIVATE
      universal_gnss_driver
  )
  add_test(
    NAME gnss_driver_test_receiver_command_transaction
    COMMAND gnss_driver_test_receiver_command_transaction
  )

  add_executable(
    gnss_driver_test_receiver_command_transaction_engine
    tests/test_receiver_command_transaction_engine.cpp
  )
  target_link_libraries(
    gnss_driver_test_receiver_command_transaction_engine
    PRIVATE
      universal_gnss_driver
  )
  add_test(
    NAME gnss_driver_test_receiver_command_transaction_engine
    COMMAND gnss_driver_test_receiver_command_transaction_engine
  )

  add_executable(
    gnss_driver_test_ubx_command_response_mapper
    tests/test_ubx_command_response_mapper.cpp
  )
  target_link_libraries(
    gnss_driver_test_ubx_command_response_mapper
    PRIVATE
      universal_gnss_driver
  )
  add_test(
    NAME gnss_driver_test_ubx_command_response_mapper
    COMMAND gnss_driver_test_ubx_command_response_mapper
  )

  add_executable(
    gnss_driver_test_ublox_config_profile_builder
    tests/test_ublox_config_profile_builder.cpp
  )
  target_link_libraries(
    gnss_driver_test_ublox_config_profile_builder
    PRIVATE
      universal_gnss_driver
  )
  add_test(
    NAME gnss_driver_test_ublox_config_profile_builder
    COMMAND gnss_driver_test_ublox_config_profile_builder
  )

  add_executable(
    gnss_driver_test_unicore_config_profile_builder
    tests/test_unicore_config_profile_builder.cpp
  )
  target_link_libraries(
    gnss_driver_test_unicore_config_profile_builder
    PRIVATE
      universal_gnss_driver
  )
  add_test(
    NAME gnss_driver_test_unicore_config_profile_builder
    COMMAND gnss_driver_test_unicore_config_profile_builder
  )
endif()
