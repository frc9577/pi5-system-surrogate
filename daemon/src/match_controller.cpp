#include "match_controller.hpp"

#include "periodic_task.hpp"

namespace dssurrogate {

const char* match_phase_name(MatchPhase p) noexcept {
  switch (p) {
    case MatchPhase::Idle:           return "idle";
    case MatchPhase::Auto:           return "auto";
    case MatchPhase::AutoTeleopGap:  return "auto_teleop_gap";
    case MatchPhase::Teleop:         return "teleop";
    case MatchPhase::MatchEnded:     return "match_ended";
  }
  return "unknown";
}

MatchController::MatchController(DaemonState& state, Durations d) noexcept
    : state_{state}, durations_{d} {}

MatchController::~MatchController() { stop(); }

void MatchController::start() {
  thread_ = std::jthread{[this](std::stop_token st) {
    run_periodic(st, std::chrono::duration_cast<std::chrono::microseconds>(
                         kTickPeriod),
                 [this] { tick(std::chrono::steady_clock::now()); });
  }};
}

void MatchController::stop() {
  if (thread_.joinable()) {
    thread_.request_stop();
    thread_.join();
  }
}

std::chrono::milliseconds MatchController::duration_for(
    MatchPhase phase) const noexcept {
  switch (phase) {
    case MatchPhase::Auto:          return durations_.auto_period;
    case MatchPhase::AutoTeleopGap: return durations_.auto_teleop_gap;
    case MatchPhase::Teleop:        return durations_.teleop_period;
    case MatchPhase::Idle:
    case MatchPhase::MatchEnded:    return std::chrono::milliseconds{0};
  }
  return std::chrono::milliseconds{0};
}

void MatchController::apply_phase(MatchPhase phase) {
  // Caller already holds mu_.
  switch (phase) {
    case MatchPhase::Auto:
      state_.set_enabled(true);
      state_.set_mode(RobotMode::Auto);
      break;
    case MatchPhase::Teleop:
      state_.set_enabled(true);
      state_.set_mode(RobotMode::Teleop);
      break;
    case MatchPhase::Idle:
    case MatchPhase::AutoTeleopGap:
    case MatchPhase::MatchEnded:
      state_.set_enabled(false);
      state_.set_mode(RobotMode::Unknown);
      break;
  }
  phase_ = phase;
}

void MatchController::publish_match_time(
    std::chrono::steady_clock::time_point now) {
  // Caller already holds mu_. WPILib convention (DriverStation.getMatchTime):
  // seconds remaining in the current Auto or Teleop period, counting down.
  // 0 in all other states (Idle/Gap/MatchEnded).
  int32_t seconds = 0;
  if (phase_ == MatchPhase::Auto || phase_ == MatchPhase::Teleop) {
    auto remaining = duration_for(phase_) - (now - phase_start_);
    if (remaining < std::chrono::milliseconds{0}) {
      remaining = std::chrono::milliseconds{0};
    }
    auto ceiling = std::chrono::ceil<std::chrono::seconds>(remaining);
    seconds = static_cast<int32_t>(ceiling.count());
  }
  state_.set_match_time(seconds);
}

void MatchController::start_match() {
  start_match(std::chrono::steady_clock::now());
}

void MatchController::start_match(std::chrono::steady_clock::time_point now) {
  std::lock_guard lk{mu_};
  match_start_ = now;
  phase_start_ = now;
  apply_phase(MatchPhase::Auto);
  publish_match_time(now);
}

void MatchController::stop_match() {
  std::lock_guard lk{mu_};
  apply_phase(MatchPhase::Idle);
  match_start_ = {};
  phase_start_ = {};
  state_.set_match_time(0);
}

void MatchController::skip_to_teleop() {
  skip_to_teleop(std::chrono::steady_clock::now());
}

void MatchController::skip_to_teleop(
    std::chrono::steady_clock::time_point now) {
  std::lock_guard lk{mu_};
  match_start_ = now - durations_.auto_period - durations_.auto_teleop_gap;
  phase_start_ = now;
  apply_phase(MatchPhase::Teleop);
  publish_match_time(now);
}

void MatchController::tick(std::chrono::steady_clock::time_point now) {
  std::lock_guard lk{mu_};
  if (phase_ == MatchPhase::Idle || phase_ == MatchPhase::MatchEnded) {
    return;
  }
  auto elapsed = now - phase_start_;
  auto duration = duration_for(phase_);
  if (elapsed < duration) {
    // Re-assert in case manual control mutated DaemonState.
    apply_phase(phase_);
    publish_match_time(now);
    return;
  }
  // Advance to the next phase aligned to the deadline so cumulative drift
  // doesn't accumulate.
  phase_start_ += duration;
  switch (phase_) {
    case MatchPhase::Auto:          apply_phase(MatchPhase::AutoTeleopGap); break;
    case MatchPhase::AutoTeleopGap: apply_phase(MatchPhase::Teleop); break;
    case MatchPhase::Teleop:        apply_phase(MatchPhase::MatchEnded); break;
    case MatchPhase::Idle:
    case MatchPhase::MatchEnded:    break;
  }
  publish_match_time(now);
}

MatchController::Snapshot MatchController::snapshot(
    std::chrono::steady_clock::time_point now) const {
  std::lock_guard lk{mu_};
  if (phase_ == MatchPhase::Idle) {
    return Snapshot{phase_, {}, {}, {}};
  }
  auto duration = duration_for(phase_);
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - phase_start_);
  auto total = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - match_start_);
  auto remaining = duration - elapsed;
  if (remaining < std::chrono::milliseconds{0}) {
    remaining = std::chrono::milliseconds{0};
  }
  return Snapshot{phase_, elapsed, total, remaining};
}

}  // namespace dssurrogate
