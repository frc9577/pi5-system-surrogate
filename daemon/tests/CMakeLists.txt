add_executable(ds_surrogate_tests
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
target_link_libraries(ds_surrogate_tests PRIVATE
  ds_surrogate_core
  GTest::gtest_main
)
ds_surrogate_apply_rpath(ds_surrogate_tests)

include(GoogleTest)
gtest_discover_tests(ds_surrogate_tests)
