#pragma once

#include "cadence_tracker.hpp"
#include "daemon_state.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <span>
#include <thread>

namespace surrogate {

// Periodic publisher of /Netcomm/Control/ControlData. Owns its own jthread.
//
// The "sink" abstraction decouples NT plumbing from the publisher's job:
// production wires it to nt::RawPublisher, tests wire it to a vector that
// captures bytes for inspection. Either way the publisher is fully unit
// testable without an NT instance.
class ControlDataPublisher {
 public:
  using ByteSink = std::function<void(std::span<const std::byte>)>;

  ControlDataPublisher(DaemonState& state, ByteSink sink) noexcept;
  ~ControlDataPublisher();

  ControlDataPublisher(const ControlDataPublisher&) = delete;
  ControlDataPublisher& operator=(const ControlDataPublisher&) = delete;

  // Start the publisher loop with the given tick period. Spawns a jthread.
  void start(std::chrono::microseconds period);

  // Request the publisher to stop and join its thread. Idempotent.
  void stop();

  const CadenceTracker& cadence() const noexcept { return cadence_; }

 private:
  void run(std::stop_token st, std::chrono::microseconds period);

  DaemonState& state_;
  ByteSink sink_;
  CadenceTracker cadence_;
  std::jthread thread_;
};

}  // namespace surrogate
