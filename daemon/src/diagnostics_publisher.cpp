#include "diagnostics_publisher.hpp"

#include "nt_topics.hpp"
#include "periodic_task.hpp"

namespace surrogate {

DiagnosticsPublisher::DiagnosticsPublisher(wpi::nt::NetworkTableInstance& inst,
                                           const CadenceTracker& cadence,
                                           std::string_view build_info)
    : cadence_{cadence},
      start_time_{std::chrono::steady_clock::now()},
      heartbeat_pub_{inst.GetIntegerTopic(topics::kDiagHeartbeat).Publish()},
      cadence_p99_pub_{inst.GetDoubleTopic(topics::kDiagP99).Publish()},
      cadence_p50_pub_{inst.GetDoubleTopic(topics::kDiagP50).Publish()},
      uptime_pub_{inst.GetDoubleTopic(topics::kDiagUptime).Publish()},
      build_info_pub_{inst.GetStringTopic(topics::kDiagBuildInfo).Publish()} {
  build_info_pub_.Set(build_info);
}

DiagnosticsPublisher::~DiagnosticsPublisher() { stop(); }

void DiagnosticsPublisher::start() {
  thread_ = std::jthread{[this](std::stop_token st) { run(st); }};
}

void DiagnosticsPublisher::stop() {
  if (thread_.joinable()) {
    thread_.request_stop();
    thread_.join();
  }
}

void DiagnosticsPublisher::tick() {
  heartbeat_pub_.Set(heartbeat_.fetch_add(1, std::memory_order_relaxed) + 1);
  cadence_p99_pub_.Set(cadence_.p99_ms());
  cadence_p50_pub_.Set(cadence_.p50_ms());
  auto uptime = std::chrono::steady_clock::now() - start_time_;
  uptime_pub_.Set(
      std::chrono::duration_cast<std::chrono::duration<double>>(uptime).count());
}

void DiagnosticsPublisher::run(std::stop_token st) {
  run_periodic(st, kTickPeriod, [this] { tick(); });
}

}  // namespace surrogate
