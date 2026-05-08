#pragma once

#include "cadence_tracker.hpp"
#include "wpi/nt/DoubleTopic.hpp"
#include "wpi/nt/IntegerTopic.hpp"
#include "wpi/nt/NetworkTableInstance.hpp"
#include "wpi/nt/StringTopic.hpp"

#include <atomic>
#include <chrono>
#include <string_view>
#include <thread>

namespace surrogate {

// Publishes /dev/diag/* topics at 1 Hz so external tools (AdvantageScope,
// ntcli) can verify the daemon is alive without HTTP.
//
//   /dev/diag/heartbeat       int64    monotonic, increments every tick
//   /dev/diag/cadence_p99_ms  double   from the publisher's CadenceTracker
//   /dev/diag/cadence_p50_ms  double
//   /dev/diag/uptime_s        double
//   /dev/diag/build_info      string   <git_sha> <iso_build_time>
class DiagnosticsPublisher {
 public:
  static constexpr std::chrono::milliseconds kTickPeriod{1000};

  DiagnosticsPublisher(wpi::nt::NetworkTableInstance& inst,
                       const CadenceTracker& cadence,
                       std::string_view build_info);
  ~DiagnosticsPublisher();

  DiagnosticsPublisher(const DiagnosticsPublisher&) = delete;
  DiagnosticsPublisher& operator=(const DiagnosticsPublisher&) = delete;

  void start();
  void stop();

  // Single tick — public for tests.
  void tick();

 private:
  void run(std::stop_token st);

  const CadenceTracker& cadence_;
  std::chrono::steady_clock::time_point start_time_;
  std::atomic<int64_t> heartbeat_{0};

  wpi::nt::IntegerPublisher heartbeat_pub_;
  wpi::nt::DoublePublisher cadence_p99_pub_;
  wpi::nt::DoublePublisher cadence_p50_pub_;
  wpi::nt::DoublePublisher uptime_pub_;
  wpi::nt::StringPublisher build_info_pub_;

  std::jthread thread_;
};

}  // namespace surrogate
