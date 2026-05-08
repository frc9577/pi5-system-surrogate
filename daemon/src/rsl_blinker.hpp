#pragma once

#include "daemon_state.hpp"
#include "smartio_state.hpp"

#include <chrono>
#include <thread>

namespace dssurrogate {

// Drives the Robot Signal Light line based on DaemonState's enable bit.
// FRC convention: solid ON when disabled, ~1 Hz blink when enabled. The
// HAL has no /Netcomm topic for the RSL — it's purely daemon-driven, just
// like real Limelight system-server.
class RslBlinker {
 public:
  static constexpr std::chrono::milliseconds kTickPeriod{20};
  static constexpr std::chrono::milliseconds kBlinkHalfPeriod{500};

  RslBlinker(DaemonState& state, IGpioBackend& backend, int gpio) noexcept;
  ~RslBlinker();

  RslBlinker(const RslBlinker&) = delete;
  RslBlinker& operator=(const RslBlinker&) = delete;

  void start();
  void stop();

  // Pure logic: given enabled state and elapsed time since start, what
  // should the RSL line be? Public so tests don't need to spin a thread.
  static bool desired_level(bool enabled, std::chrono::milliseconds elapsed) noexcept;

 private:
  void run(std::stop_token st);

  DaemonState& state_;
  IGpioBackend& backend_;
  int gpio_;
  std::jthread thread_;
};

}  // namespace dssurrogate
