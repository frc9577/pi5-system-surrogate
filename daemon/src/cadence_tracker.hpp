#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace surrogate {

// Rolling cadence tracker for the periodic ControlData publisher. Stores
// the last N intervals (us) in a fixed-size ring; computes percentiles by
// copying + sorting on demand. N=1024 chosen as ~5 s of history at 5 ms
// tick.
//
// Not thread-safe — owned by a single thread (the publisher).
class CadenceTracker {
 public:
  static constexpr std::size_t kCapacity = 1024;

  void record_interval_us(int64_t us) noexcept;

  std::size_t sample_count() const noexcept { return count_; }
  double p50_ms() const noexcept { return percentile_ms(0.50); }
  double p99_ms() const noexcept { return percentile_ms(0.99); }
  double p999_ms() const noexcept { return percentile_ms(0.999); }
  double max_ms() const noexcept;

 private:
  // Sort + cache a snapshot of `samples_us_` if it's stale relative to the
  // last record_interval_us call. Lazy: only triggers the sort on the first
  // percentile read after a write. Subsequent reads in the same epoch are
  // O(1).
  void refresh_sorted() const noexcept;
  double percentile_ms(double p) const noexcept;

  std::array<int64_t, kCapacity> samples_us_{};
  // Sorted snapshot of `samples_us_[0..count_)` produced lazily on read.
  // Mutable because `refresh_sorted()` is called from const accessors.
  mutable std::array<int64_t, kCapacity> sorted_cache_{};
  mutable std::size_t cached_epoch_ = 0;
  std::size_t epoch_ = 0;        // increments on every record_interval_us
  std::size_t next_ = 0;
  std::size_t count_ = 0;
};

}  // namespace surrogate
