#include "diagnostics_publisher.hpp"

#include "cadence_tracker.hpp"
#include "nt_server.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using surrogate::CadenceTracker;
using surrogate::DiagnosticsPublisher;
using namespace std::chrono_literals;

TEST(Diagnostics, HeartbeatIncrementsMonotonically) {
  surrogate::NtServer server{56830};
  CadenceTracker cadence;
  DiagnosticsPublisher diag{server.instance(), cadence, "test-build"};

  auto sub = server.instance()
                 .GetIntegerTopic("/dev/diag/heartbeat")
                 .Subscribe(-1);
  diag.tick();
  std::this_thread::sleep_for(15ms);
  int64_t a = sub.Get();
  diag.tick();
  std::this_thread::sleep_for(15ms);
  int64_t b = sub.Get();
  diag.tick();
  std::this_thread::sleep_for(15ms);
  int64_t c = sub.Get();
  EXPECT_EQ(a, 1);
  EXPECT_EQ(b, 2);
  EXPECT_EQ(c, 3);
}

TEST(Diagnostics, CadenceP99ReflectsTrackerState) {
  surrogate::NtServer server{56831};
  CadenceTracker cadence;
  for (int i = 0; i < 200; ++i) cadence.record_interval_us(7000);
  DiagnosticsPublisher diag{server.instance(), cadence, "test"};

  auto sub = server.instance()
                 .GetDoubleTopic("/dev/diag/cadence_p99_ms")
                 .Subscribe(-1.0);
  diag.tick();
  std::this_thread::sleep_for(15ms);
  EXPECT_DOUBLE_EQ(sub.Get(), 7.0);
}

TEST(Diagnostics, BuildInfoPublishedAtConstruction) {
  surrogate::NtServer server{56832};
  CadenceTracker cadence;
  DiagnosticsPublisher diag{server.instance(), cadence,
                            "abc123 2026-05-08T12:00:00Z g++-16"};
  auto sub = server.instance()
                 .GetStringTopic("/dev/diag/build_info")
                 .Subscribe("");
  std::this_thread::sleep_for(15ms);
  EXPECT_EQ(sub.Get(), "abc123 2026-05-08T12:00:00Z g++-16");
}

TEST(Diagnostics, UptimeMonotonicallyIncreases) {
  surrogate::NtServer server{56833};
  CadenceTracker cadence;
  DiagnosticsPublisher diag{server.instance(), cadence, "t"};

  auto sub =
      server.instance().GetDoubleTopic("/dev/diag/uptime_s").Subscribe(-1.0);
  diag.tick();
  std::this_thread::sleep_for(15ms);
  double a = sub.Get();
  std::this_thread::sleep_for(50ms);
  diag.tick();
  std::this_thread::sleep_for(15ms);
  double b = sub.Get();
  EXPECT_GT(b, a);
}

TEST(Diagnostics, RunLoopStartsAndStopsCleanly) {
  surrogate::NtServer server{56834};
  CadenceTracker cadence;
  for (int i = 0; i < 50; ++i) cadence.record_interval_us(5000);

  DiagnosticsPublisher diag{server.instance(), cadence, "t"};
  diag.start();
  // Period is 1000 ms — too long to actually catch a tick in CI without
  // sleeping a full second. We just verify start/stop don't crash.
  std::this_thread::sleep_for(50ms);
  diag.stop();
  SUCCEED();
}
