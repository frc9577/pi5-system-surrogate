#include "match_controller.hpp"

#include "daemon_state.hpp"

#include <gtest/gtest.h>

#include <chrono>

using dssurrogate::DaemonState;
using dssurrogate::MatchController;
using dssurrogate::MatchPhase;
using dssurrogate::RobotMode;
using namespace std::chrono_literals;

namespace {

// Compact "test match": 2s auto, 1s gap, 3s teleop. Lets transition tests
// run in <10 ms of simulated time without polluting wallclock.
constexpr MatchController::Durations kShort{
    .auto_period = 2'000ms,
    .auto_teleop_gap = 1'000ms,
    .teleop_period = 3'000ms,
};

const auto t0 = std::chrono::steady_clock::time_point{};

}  // namespace

TEST(MatchController, DefaultPhaseIsIdle) {
  DaemonState s;
  MatchController c{s, kShort};
  EXPECT_EQ(c.snapshot(t0).phase, MatchPhase::Idle);
  auto v = s.snapshot_control();
  EXPECT_FALSE(v.enabled);
  EXPECT_EQ(v.mode, RobotMode::Unknown);
}

TEST(MatchController, StartMatchTransitionsToAutoEnabled) {
  DaemonState s;
  MatchController c{s, kShort};
  c.start_match(t0);
  EXPECT_EQ(c.snapshot(t0).phase, MatchPhase::Auto);
  auto v = s.snapshot_control();
  EXPECT_TRUE(v.enabled);
  EXPECT_EQ(v.mode, RobotMode::Auto);
}

TEST(MatchController, RemainsInAutoBeforeDuration) {
  DaemonState s;
  MatchController c{s, kShort};
  c.start_match(t0);
  c.tick(t0 + 1'500ms);
  EXPECT_EQ(c.snapshot(t0 + 1'500ms).phase, MatchPhase::Auto);
}

TEST(MatchController, AutoToGapAtDuration) {
  DaemonState s;
  MatchController c{s, kShort};
  c.start_match(t0);
  c.tick(t0 + 2'001ms);
  EXPECT_EQ(c.snapshot(t0 + 2'001ms).phase, MatchPhase::AutoTeleopGap);
  auto v = s.snapshot_control();
  EXPECT_FALSE(v.enabled);
  EXPECT_EQ(v.mode, RobotMode::Unknown);
}

TEST(MatchController, GapToTeleop) {
  DaemonState s;
  MatchController c{s, kShort};
  c.start_match(t0);
  c.tick(t0 + 2'001ms);  // → gap
  c.tick(t0 + 3'001ms);  // → teleop (2s auto + 1s gap)
  EXPECT_EQ(c.snapshot(t0 + 3'001ms).phase, MatchPhase::Teleop);
  auto v = s.snapshot_control();
  EXPECT_TRUE(v.enabled);
  EXPECT_EQ(v.mode, RobotMode::Teleop);
}

TEST(MatchController, TeleopToMatchEnded) {
  DaemonState s;
  MatchController c{s, kShort};
  c.start_match(t0);
  // Walk through Auto → Gap → Teleop → MatchEnded.
  c.tick(t0 + 2'001ms);
  c.tick(t0 + 3'001ms);
  c.tick(t0 + 6'001ms);  // teleop ends at 6s (2 + 1 + 3)
  EXPECT_EQ(c.snapshot(t0 + 6'001ms).phase, MatchPhase::MatchEnded);
  auto v = s.snapshot_control();
  EXPECT_FALSE(v.enabled);
}

TEST(MatchController, MatchEndedIsTerminal) {
  DaemonState s;
  MatchController c{s, kShort};
  c.start_match(t0);
  c.tick(t0 + 2'001ms);
  c.tick(t0 + 3'001ms);
  c.tick(t0 + 6'001ms);
  // Any further tick should keep us in MatchEnded.
  c.tick(t0 + 10s);
  c.tick(t0 + 60s);
  EXPECT_EQ(c.snapshot(t0 + 60s).phase, MatchPhase::MatchEnded);
}

TEST(MatchController, StopMatchReturnsToIdle) {
  DaemonState s;
  MatchController c{s, kShort};
  c.start_match(t0);
  c.tick(t0 + 1s);
  c.stop_match();
  EXPECT_EQ(c.snapshot(t0 + 1s).phase, MatchPhase::Idle);
  EXPECT_FALSE(s.snapshot_control().enabled);
}

TEST(MatchController, SkipToTeleopBypasssAutoAndGap) {
  DaemonState s;
  MatchController c{s, kShort};
  c.skip_to_teleop(t0);
  EXPECT_EQ(c.snapshot(t0).phase, MatchPhase::Teleop);
  EXPECT_TRUE(s.snapshot_control().enabled);
  EXPECT_EQ(s.snapshot_control().mode, RobotMode::Teleop);
  // Teleop still ends at the configured duration after skip.
  c.tick(t0 + 3'001ms);
  EXPECT_EQ(c.snapshot(t0 + 3'001ms).phase, MatchPhase::MatchEnded);
}

TEST(MatchController, SnapshotReportsRemainingTime) {
  DaemonState s;
  MatchController c{s, kShort};
  c.start_match(t0);
  auto snap = c.snapshot(t0 + 500ms);
  EXPECT_EQ(snap.phase, MatchPhase::Auto);
  EXPECT_EQ(snap.elapsed_in_phase, 500ms);
  EXPECT_EQ(snap.remaining_in_phase, 1'500ms);
  EXPECT_EQ(snap.total_elapsed, 500ms);
}

TEST(MatchController, TickReassertsDaemonStateAgainstManualOverride) {
  // Mid-Auto, simulate someone clicking "Disable" via the web UI. The
  // controller's next tick must put us back into the correct Auto state.
  DaemonState s;
  MatchController c{s, kShort};
  c.start_match(t0);
  s.set_enabled(false);
  s.set_mode(RobotMode::Unknown);
  c.tick(t0 + 100ms);
  auto v = s.snapshot_control();
  EXPECT_TRUE(v.enabled);
  EXPECT_EQ(v.mode, RobotMode::Auto);
}

TEST(MatchController, MatchTimeCountsDownInAuto) {
  // Per WPILib DriverStation.getMatchTime() — seconds remaining in the
  // current Auto/Teleop period, counting down. Round up to whole seconds.
  DaemonState s;
  MatchController c{s, kShort};
  c.start_match(t0);
  EXPECT_EQ(s.snapshot_control().match_time_s, 2);  // 2s auto, just started
  c.tick(t0 + 500ms);
  EXPECT_EQ(s.snapshot_control().match_time_s, 2);  // 1.5s remaining → ceil 2
  c.tick(t0 + 1'500ms);
  EXPECT_EQ(s.snapshot_control().match_time_s, 1);  // 0.5s remaining → ceil 1
  c.tick(t0 + 1'999ms);
  EXPECT_EQ(s.snapshot_control().match_time_s, 1);  // 1ms remaining → ceil 1
}

TEST(MatchController, MatchTimeIsZeroInDisabledPhases) {
  DaemonState s;
  MatchController c{s, kShort};
  c.start_match(t0);
  c.tick(t0 + 2'001ms);  // → AutoTeleopGap
  EXPECT_EQ(c.snapshot(t0 + 2'001ms).phase, MatchPhase::AutoTeleopGap);
  EXPECT_EQ(s.snapshot_control().match_time_s, 0);
  c.tick(t0 + 3'001ms);  // → Teleop
  EXPECT_EQ(s.snapshot_control().match_time_s, 3);  // 3s teleop, just started
  c.tick(t0 + 6'001ms);  // → MatchEnded
  EXPECT_EQ(s.snapshot_control().match_time_s, 0);
  c.stop_match();
  EXPECT_EQ(s.snapshot_control().match_time_s, 0);
}

TEST(MatchController, TickInIdleIsNoOp) {
  DaemonState s;
  s.set_enabled(true);
  s.set_mode(RobotMode::Test);
  MatchController c{s, kShort};
  c.tick(t0);
  c.tick(t0 + 5s);
  // Manual state preserved — controller doesn't touch DaemonState in Idle.
  auto v = s.snapshot_control();
  EXPECT_TRUE(v.enabled);
  EXPECT_EQ(v.mode, RobotMode::Test);
}

TEST(MatchController, ThreadStartStopIsClean) {
  DaemonState s;
  MatchController c{s, kShort};
  c.start();
  std::this_thread::sleep_for(60ms);  // exercise the periodic loop
  c.stop();
  SUCCEED();
}
