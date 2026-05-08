#pragma once

#include <chrono>
#include <stop_token>
#include <thread>
#include <utility>

namespace surrogate {

// Runs `tick` on a fixed cadence until `st` requests stop. Used by every
// periodic publisher in the daemon (controldata, diagnostics, rsl,
// smartio, gamepad). The skeleton was repeated five times before this
// helper existed; consolidating here puts the cadence + stop semantics
// in one place to audit.
//
// Drift handling: deadline += period each iteration, so a slow tick gets
// caught up by skipping to the next aligned slot rather than slipping
// permanently. If multiple deadlines have passed (a long pause), the
// loop fires once per backlog tick — callers should keep tick() cheap.
template <typename TickFn>
void run_periodic(std::stop_token st,
                  std::chrono::microseconds period,
                  TickFn&& tick) {
  using clock = std::chrono::steady_clock;
  auto deadline = clock::now() + period;
  while (!st.stop_requested()) {
    std::this_thread::sleep_until(deadline);
    if (st.stop_requested()) break;
    deadline += period;
    std::forward<TickFn>(tick)();
  }
}

}  // namespace surrogate
