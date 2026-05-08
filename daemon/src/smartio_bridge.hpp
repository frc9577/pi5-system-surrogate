#pragma once

#include "smartio_state.hpp"
#include "wpi/nt/IntegerTopic.hpp"
#include "wpi/nt/NetworkTableInstance.hpp"

#include <chrono>
#include <memory>
#include <span>
#include <thread>
#include <vector>

namespace surrogate {

// Bridges /io/{ch}/* NT topics to a SmartIoChannel state machine driving
// libgpiod. One channel slot per (channel, gpio) entry in `mappings`.
//
// On each tick():
//   1. read /io/{ch}/type — if changed since last tick, dispatch to
//      channel.on_type_changed
//   2. read /io/{ch}/valset — if changed, dispatch to channel.on_valset
//   3. for input channels, sample via channel.sample_input and publish
//      the result to /io/{ch}/valget
//
// The bridge owns its own jthread when start() is called; tick() is also
// public so tests can drive it deterministically.
class SmartIoBridge {
 public:
  struct PinMapping {
    int channel;
    int gpio;
  };

  static constexpr std::chrono::milliseconds kTickPeriod{20};

  SmartIoBridge(wpi::nt::NetworkTableInstance& inst, IGpioBackend& backend,
                std::span<const PinMapping> mappings);
  ~SmartIoBridge();

  SmartIoBridge(const SmartIoBridge&) = delete;
  SmartIoBridge& operator=(const SmartIoBridge&) = delete;

  void start();
  void stop();

  // Single dispatch pass — public for tests. Not thread-safe relative to
  // the running thread; tests should call this on a non-started bridge.
  void tick();

  std::size_t channel_count() const noexcept { return channels_.size(); }

  // Number of channels currently configured as DigitalInput or DigitalOutput.
  std::size_t channels_configured() const noexcept;

 private:
  // Per-channel glue: NT topics, last-seen values, and the state machine.
  // Wrapped in unique_ptr because SmartIoChannel holds a reference, making
  // it neither copyable nor movable.
  struct ChannelGlue {
    SmartIoChannel channel;
    wpi::nt::IntegerSubscriber type_sub;
    wpi::nt::IntegerSubscriber valset_sub;
    wpi::nt::IntegerPublisher valget_pub;
    int64_t last_type;
    int64_t last_valset;
  };

  void run(std::stop_token st);

  std::vector<std::unique_ptr<ChannelGlue>> channels_;
  std::jthread thread_;
};

}  // namespace surrogate
