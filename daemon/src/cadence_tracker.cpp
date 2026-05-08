#include "cadence_tracker.hpp"

#include <algorithm>

namespace surrogate {

void CadenceTracker::record_interval_us(int64_t us) noexcept {
  samples_us_[next_] = us;
  next_ = (next_ + 1) % kCapacity;
  if (count_ < kCapacity) {
    ++count_;
  }
  ++epoch_;
}

void CadenceTracker::refresh_sorted() const noexcept {
  if (cached_epoch_ == epoch_) {
    return;
  }
  std::copy_n(samples_us_.begin(), count_, sorted_cache_.begin());
  std::sort(sorted_cache_.begin(),
            sorted_cache_.begin() + static_cast<std::ptrdiff_t>(count_));
  cached_epoch_ = epoch_;
}

double CadenceTracker::percentile_ms(double p) const noexcept {
  if (count_ == 0) {
    return 0.0;
  }
  refresh_sorted();
  auto idx = static_cast<std::size_t>(p * static_cast<double>(count_));
  if (idx >= count_) idx = count_ - 1;
  return static_cast<double>(sorted_cache_[idx]) / 1000.0;
}

double CadenceTracker::max_ms() const noexcept {
  if (count_ == 0) {
    return 0.0;
  }
  refresh_sorted();
  return static_cast<double>(sorted_cache_[count_ - 1]) / 1000.0;
}

}  // namespace surrogate
