#include "control_data_publisher.hpp"

#include "control_data_encoder.hpp"
#include "periodic_task.hpp"

#include <array>
#include <utility>

namespace dssurrogate {

ControlDataPublisher::ControlDataPublisher(DaemonState& state,
                                           ByteSink sink) noexcept
    : state_{state}, sink_{std::move(sink)} {}

ControlDataPublisher::~ControlDataPublisher() { stop(); }

void ControlDataPublisher::start(std::chrono::microseconds period) {
  thread_ = std::jthread{[this, period](std::stop_token st) { run(st, period); }};
}

void ControlDataPublisher::stop() {
  if (thread_.joinable()) {
    thread_.request_stop();
    thread_.join();
  }
}

void ControlDataPublisher::run(std::stop_token st,
                               std::chrono::microseconds period) {
  using clock = std::chrono::steady_clock;
  auto last = clock::now();
  run_periodic(st, period, [this, &last] {
    auto now = clock::now();
    cadence_.record_interval_us(
        std::chrono::duration_cast<std::chrono::microseconds>(now - last)
            .count());
    last = now;

    // Snapshot under DaemonState's mutex; encode + publish without it.
    ControlDataView view = state_.snapshot_control();
    std::array<std::byte, kControlDataMaxBytes> buf{};
    std::size_t n = encode_control_data(view, buf);
    if (n == 0) return;
    sink_(std::span<const std::byte>{buf.data(), n});
  });
}

}  // namespace dssurrogate
