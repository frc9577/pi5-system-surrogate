#include "daemon_state.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

using dssurrogate::DaemonState;
using dssurrogate::RobotMode;

TEST(DaemonState, DefaultsAreSafe) {
  DaemonState s;
  auto v = s.snapshot_control();
  EXPECT_FALSE(v.enabled);
  EXPECT_FALSE(v.e_stop);
  EXPECT_FALSE(v.fms_connected);
  EXPECT_TRUE(v.ds_connected) << "ds_connected must always be true to "
                                 "prevent the HAL from zeroing the control word";
  EXPECT_EQ(v.mode, RobotMode::Unknown);
  EXPECT_EQ(v.alliance, 0);
  EXPECT_EQ(v.match_time_s, 0);
}

TEST(DaemonState, SettersReflectInSnapshot) {
  DaemonState s;
  s.set_enabled(true);
  s.set_mode(RobotMode::Teleop);
  s.set_alliance(2);
  s.set_match_time(15);
  auto v = s.snapshot_control();
  EXPECT_TRUE(v.enabled);
  EXPECT_EQ(v.mode, RobotMode::Teleop);
  EXPECT_EQ(v.alliance, 2);
  EXPECT_EQ(v.match_time_s, 15);
}

// The watchdog-active signal is no longer a struct field; it's encoded
// from `enabled` directly in pack_control_word. This test now lives in
// test_control_data_encoder.cpp as part of the encoder contract.

TEST(DaemonState, EStopLatchesIndependently) {
  DaemonState s;
  s.set_enabled(true);
  s.set_e_stop(true);
  auto v = s.snapshot_control();
  EXPECT_TRUE(v.enabled);
  EXPECT_TRUE(v.e_stop);
}

TEST(DaemonState, ConcurrentReadersAndWritersAreFieldLevelAtomic) {
  // Independent setters do not promise cross-field atomicity — a reader
  // can see (new_mode, old_alliance) between two setters. What we DO
  // promise: every individual field reads a value that some writer wrote
  // (no UB, no torn primitive). Verify by writing distinct values per
  // field and asserting each independently lands in the valid set.
  DaemonState s;
  std::atomic<bool> stop = false;

  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back([&] {
      while (!stop.load(std::memory_order_relaxed)) {
        auto v = s.snapshot_control();
        ASSERT_GE(static_cast<int>(v.mode), 0);
        ASSERT_LE(static_cast<int>(v.mode), 3);
        ASSERT_GE(v.alliance, 0);
        ASSERT_LE(v.alliance, 3);
      }
    });
  }
  threads.emplace_back([&] {
    for (int n = 0; n < 5'000; ++n) s.set_mode(static_cast<RobotMode>(n & 3));
  });
  threads.emplace_back([&] {
    for (int n = 0; n < 5'000; ++n) s.set_alliance(n & 3);
  });
  threads.emplace_back([&] {
    for (int n = 0; n < 5'000; ++n) s.set_enabled(n & 1);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  stop.store(true);
  for (auto& t : threads) t.join();
}
