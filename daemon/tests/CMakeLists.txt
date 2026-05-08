add_executable(daemon_tests
  test_smoke.cpp
  test_control_data_encoder.cpp
  test_daemon_state.cpp
  test_smartio_state.cpp
  test_cadence_tracker.cpp
  test_control_data_publisher.cpp
  test_nt_server.cpp
  test_rsl_blinker.cpp
  test_smartio_bridge.cpp
  test_gamepad_ingest.cpp
  test_diagnostics_publisher.cpp
  test_web_ui.cpp
  test_linux_runtime.cpp
  test_imu_publisher.cpp
  test_match_controller.cpp
)
target_link_libraries(daemon_tests PRIVATE
  daemon_core
  GTest::gtest_main
)
daemon_apply_rpath(daemon_tests)

include(GoogleTest)
gtest_discover_tests(daemon_tests)
