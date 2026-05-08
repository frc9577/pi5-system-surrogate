#pragma once

#include "daemon_state.hpp"
#include "wpi/nt/BooleanTopic.hpp"
#include "wpi/nt/IntegerTopic.hpp"
#include "wpi/nt/NetworkTableInstance.hpp"

#include <chrono>
#include <thread>

namespace dssurrogate {

// Subscribes to /dev/control/* topics (written by the helper-laptop
// gamepad reader and the web UI) and applies them to DaemonState.
//
// First-cut topic schema:
//   /dev/control/enabled  (bool)
//   /dev/control/mode     (int)  1=Auto, 2=Teleop, 3=Test, else Unknown
//   /dev/control/alliance (int)  0..3 on the wire
//   /dev/control/estop    (bool)
//
// Joystick axes/buttons/POVs (/dev/gamepad/{0..5}/*) are deferred — the
// daemon's first-cut goal is the HAL contract; full joystick support
// requires extending the protobuf encoder with pb_callback_t writers.
class GamepadIngest {
 public:
  static constexpr std::chrono::milliseconds kTickPeriod{20};

  GamepadIngest(wpi::nt::NetworkTableInstance& inst, DaemonState& state);
  ~GamepadIngest();

  GamepadIngest(const GamepadIngest&) = delete;
  GamepadIngest& operator=(const GamepadIngest&) = delete;

  void start();
  void stop();
  void tick();

 private:
  void run(std::stop_token st);

  // Cached values to suppress no-op DaemonState writes — match
  // SmartIoBridge's change-detection pattern. Sentinel values mean
  // "never seen yet" so the first tick always propagates.
  static constexpr int64_t kIntSentinel = -9999;

  DaemonState& state_;
  wpi::nt::BooleanSubscriber enabled_sub_;
  wpi::nt::IntegerSubscriber mode_sub_;
  wpi::nt::IntegerSubscriber alliance_sub_;
  wpi::nt::BooleanSubscriber estop_sub_;
  bool last_enabled_seen_ = false;
  bool last_estop_seen_ = false;
  int64_t last_mode_seen_ = kIntSentinel;
  int64_t last_alliance_seen_ = kIntSentinel;
  bool first_tick_ = true;
  std::jthread thread_;
};

}  // namespace dssurrogate
