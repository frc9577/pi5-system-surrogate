#include "rsl_blinker.hpp"

#include "periodic_task.hpp"

namespace surrogate {

RslBlinker::RslBlinker(DaemonState& state, IGpioBackend& backend,
                       int gpio) noexcept
    : state_{state}, backend_{backend}, gpio_{gpio} {}

RslBlinker::~RslBlinker() { stop(); }

void RslBlinker::start() {
  // RSL pin is owned by us, not by SmartIo. Configure as output.
  backend_.request_output(gpio_, /*initial_value=*/true);
  thread_ = std::jthread{[this](std::stop_token st) { run(st); }};
}

void RslBlinker::stop() {
  if (thread_.joinable()) {
    thread_.request_stop();
    thread_.join();
    backend_.release_line(gpio_);
  }
}

bool RslBlinker::desired_level(bool enabled,
                               std::chrono::milliseconds elapsed) noexcept {
  if (!enabled) return true;  // solid on when disabled
  // Blink: half-period on, half-period off.
  auto whole_periods = elapsed.count() / kBlinkHalfPeriod.count();
  return (whole_periods & 1) == 0;
}

void RslBlinker::run(std::stop_token st) {
  using clock = std::chrono::steady_clock;
  const auto t0 = clock::now();
  bool last = true;
  run_periodic(st, kTickPeriod, [this, t0, &last] {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        clock::now() - t0);
    bool enabled = state_.snapshot_control().enabled;
    bool want = desired_level(enabled, elapsed);
    if (want != last) {
      backend_.set_value(gpio_, want);
      last = want;
    }
  });
}

}  // namespace surrogate
