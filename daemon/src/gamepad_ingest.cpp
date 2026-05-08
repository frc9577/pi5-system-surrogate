#include "gamepad_ingest.hpp"

#include "nt_topics.hpp"
#include "periodic_task.hpp"
#include "wpi/nt/IntegerTopic.hpp"

namespace surrogate {

namespace {
constexpr int64_t kModeUnknownDefault = 0;
constexpr int64_t kAllianceDefault = 0;

RobotMode mode_from_int(int64_t v) noexcept {
  switch (v) {
    case 1: return RobotMode::Auto;
    case 2: return RobotMode::Teleop;
    case 3: return RobotMode::Test;
    default: return RobotMode::Unknown;
  }
}
}  // namespace

GamepadIngest::GamepadIngest(wpi::nt::NetworkTableInstance& inst,
                             DaemonState& state)
    : state_{state},
      enabled_sub_{
          inst.GetBooleanTopic(topics::kCtrlEnabled).Subscribe(false)},
      mode_sub_{inst.GetIntegerTopic(topics::kCtrlMode)
                    .Subscribe(kModeUnknownDefault)},
      alliance_sub_{inst.GetIntegerTopic(topics::kCtrlAlliance)
                        .Subscribe(kAllianceDefault)},
      estop_sub_{inst.GetBooleanTopic(topics::kCtrlEStop).Subscribe(false)} {}

GamepadIngest::~GamepadIngest() { stop(); }

void GamepadIngest::start() {
  thread_ = std::jthread{[this](std::stop_token st) { run(st); }};
}

void GamepadIngest::stop() {
  if (thread_.joinable()) {
    thread_.request_stop();
    thread_.join();
  }
}

void GamepadIngest::tick() {
  bool enabled = enabled_sub_.Get();
  if (first_tick_ || enabled != last_enabled_seen_) {
    state_.set_enabled(enabled);
    last_enabled_seen_ = enabled;
  }
  int64_t mode = mode_sub_.Get();
  if (first_tick_ || mode != last_mode_seen_) {
    state_.set_mode(mode_from_int(mode));
    last_mode_seen_ = mode;
  }
  int64_t alliance = alliance_sub_.Get();
  if (first_tick_ || alliance != last_alliance_seen_) {
    state_.set_alliance(static_cast<int>(alliance));
    last_alliance_seen_ = alliance;
  }
  bool estop = estop_sub_.Get();
  if (first_tick_ || estop != last_estop_seen_) {
    state_.set_e_stop(estop);
    last_estop_seen_ = estop;
  }
  first_tick_ = false;
}

void GamepadIngest::run(std::stop_token st) {
  run_periodic(st, kTickPeriod, [this] { tick(); });
}

}  // namespace surrogate
