#pragma once

#include "daemon_state.hpp"

#include <chrono>
#include <mutex>
#include <stop_token>
#include <thread>

namespace surrogate {

enum class MatchPhase {
  Idle,            // no match running
  Auto,            // enabled + mode=Auto
  AutoTeleopGap,   // disabled, brief pause between auto and teleop
  Teleop,          // enabled + mode=Teleop
  MatchEnded,      // disabled, terminal until next start_match()
};

const char* match_phase_name(MatchPhase p) noexcept;

struct MatchDurations {
  std::chrono::milliseconds auto_period{15'000};
  std::chrono::milliseconds auto_teleop_gap{3'000};
  std::chrono::milliseconds teleop_period{135'000};
};

// Drives DaemonState through real-FRC match phases on a clock. When idle,
// manual web-UI controls own DaemonState. When a match is running, this
// controller writes DaemonState every tick to reflect the current phase.
//
// Phase durations are configurable so the integration test can run a
// short "match" in seconds rather than minutes.
class MatchController {
 public:
  using Durations = MatchDurations;
  static constexpr std::chrono::milliseconds kTickPeriod{50};

  explicit MatchController(DaemonState& state,
                           Durations durations = Durations{}) noexcept;
  ~MatchController();

  MatchController(const MatchController&) = delete;
  MatchController& operator=(const MatchController&) = delete;

  void start();   // start the periodic tick thread
  void stop();    // stop the tick thread (does not affect match phase)

  void start_match();
  void start_match(std::chrono::steady_clock::time_point now);
  void stop_match();
  void skip_to_teleop();
  void skip_to_teleop(std::chrono::steady_clock::time_point now);

  // Pure-logic tick (testable without wallclock).
  void tick(std::chrono::steady_clock::time_point now);

  struct Snapshot {
    MatchPhase phase;
    std::chrono::milliseconds elapsed_in_phase;
    std::chrono::milliseconds total_elapsed;
    std::chrono::milliseconds remaining_in_phase;
  };
  Snapshot snapshot(std::chrono::steady_clock::time_point now) const;
  Snapshot snapshot() const { return snapshot(std::chrono::steady_clock::now()); }

 private:
  void apply_phase(MatchPhase phase);
  void publish_match_time(std::chrono::steady_clock::time_point now);
  std::chrono::milliseconds duration_for(MatchPhase phase) const noexcept;

  DaemonState& state_;
  Durations durations_;
  mutable std::mutex mu_;
  MatchPhase phase_ = MatchPhase::Idle;
  std::chrono::steady_clock::time_point match_start_{};
  std::chrono::steady_clock::time_point phase_start_{};
  std::jthread thread_;
};

}  // namespace surrogate
