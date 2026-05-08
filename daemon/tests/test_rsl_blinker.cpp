#include "rsl_blinker.hpp"

#include "daemon_state.hpp"
#include "smartio_state.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

using dssurrogate::DaemonState;
using dssurrogate::IGpioBackend;
using dssurrogate::RslBlinker;
using namespace std::chrono_literals;

namespace {

class TrackingBackend final : public IGpioBackend {
 public:
  std::mutex mu;
  bool requested = false;
  bool released = false;
  bool initial = false;
  std::vector<bool> values;

  void release_line(int) override {
    std::lock_guard lk{mu};
    released = true;
  }
  void request_input(int) override {}
  void request_output(int, bool v) override {
    std::lock_guard lk{mu};
    requested = true;
    initial = v;
  }
  void set_value(int, bool v) override {
    std::lock_guard lk{mu};
    values.push_back(v);
  }
  bool get_value(int) override { return false; }
};

}  // namespace

TEST(Rsl, DesiredLevelDisabledSolid) {
  EXPECT_TRUE(RslBlinker::desired_level(false, 0ms));
  EXPECT_TRUE(RslBlinker::desired_level(false, 250ms));
  EXPECT_TRUE(RslBlinker::desired_level(false, 1500ms));
  EXPECT_TRUE(RslBlinker::desired_level(false, 100s));
}

TEST(Rsl, DesiredLevelEnabledTogglesEachHalfPeriod) {
  EXPECT_TRUE(RslBlinker::desired_level(true, 0ms));
  EXPECT_TRUE(RslBlinker::desired_level(true, 250ms));
  EXPECT_FALSE(RslBlinker::desired_level(true, 500ms));
  EXPECT_FALSE(RslBlinker::desired_level(true, 750ms));
  EXPECT_TRUE(RslBlinker::desired_level(true, 1000ms));
  EXPECT_FALSE(RslBlinker::desired_level(true, 1500ms));
}

TEST(Rsl, StartRequestsOutputHighThenStopReleases) {
  DaemonState s;
  TrackingBackend b;
  RslBlinker rsl{s, b, 26};
  rsl.start();
  std::this_thread::sleep_for(30ms);
  rsl.stop();
  std::lock_guard lk{b.mu};
  EXPECT_TRUE(b.requested);
  EXPECT_TRUE(b.initial);  // solid-on at startup
  EXPECT_TRUE(b.released);
}

TEST(Rsl, EnabledStateProducesToggles) {
  DaemonState s;
  TrackingBackend b;
  RslBlinker rsl{s, b, 26};
  s.set_enabled(true);
  rsl.start();
  // 1.2 s should yield at least 2 toggle events at 500 ms half-period.
  std::this_thread::sleep_for(1200ms);
  rsl.stop();
  std::lock_guard lk{b.mu};
  EXPECT_GE(b.values.size(), 2u);
}
