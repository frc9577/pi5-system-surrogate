#include "cadence_tracker.hpp"

#include <gtest/gtest.h>

using surrogate::CadenceTracker;

TEST(Cadence, EmptyReportsZero) {
  CadenceTracker t;
  EXPECT_EQ(t.sample_count(), 0u);
  EXPECT_DOUBLE_EQ(t.p50_ms(), 0.0);
  EXPECT_DOUBLE_EQ(t.p99_ms(), 0.0);
  EXPECT_DOUBLE_EQ(t.p999_ms(), 0.0);
  EXPECT_DOUBLE_EQ(t.max_ms(), 0.0);
}

TEST(Cadence, SteadyTicksReportConstantInterval) {
  CadenceTracker t;
  for (int i = 0; i < 500; ++i) t.record_interval_us(5000);  // 5 ms
  EXPECT_EQ(t.sample_count(), 500u);
  EXPECT_DOUBLE_EQ(t.p50_ms(), 5.0);
  EXPECT_DOUBLE_EQ(t.p99_ms(), 5.0);
  EXPECT_DOUBLE_EQ(t.max_ms(), 5.0);
}

TEST(Cadence, MaxCapturesSingleOutlier) {
  CadenceTracker t;
  for (int i = 0; i < 999; ++i) t.record_interval_us(5000);
  t.record_interval_us(50'000);
  EXPECT_DOUBLE_EQ(t.max_ms(), 50.0);
  EXPECT_DOUBLE_EQ(t.p50_ms(), 5.0);
  EXPECT_DOUBLE_EQ(t.p99_ms(), 5.0);
}

TEST(Cadence, P999ReachesOutlierWithOneInThousand) {
  CadenceTracker t;
  for (int i = 0; i < 999; ++i) t.record_interval_us(5000);
  t.record_interval_us(50'000);
  // p999 of 1000 samples → idx = 999 → the outlier.
  EXPECT_DOUBLE_EQ(t.p999_ms(), 50.0);
}

TEST(Cadence, RingDropsOldSamplesAtCapacity) {
  CadenceTracker t;
  for (int i = 0; i < 100; ++i) t.record_interval_us(50'000);  // bad
  EXPECT_DOUBLE_EQ(t.max_ms(), 50.0);
  for (std::size_t i = 0; i < CadenceTracker::kCapacity; ++i) {
    t.record_interval_us(5000);
  }
  EXPECT_EQ(t.sample_count(), CadenceTracker::kCapacity);
  EXPECT_DOUBLE_EQ(t.max_ms(), 5.0)
      << "old 50ms samples should have rolled out of the ring";
}

TEST(Cadence, MixedDistributionPercentiles) {
  CadenceTracker t;
  // 800 ticks at 5ms, 200 at 7ms — p99 should pick up the 7ms tail.
  for (int i = 0; i < 800; ++i) t.record_interval_us(5000);
  for (int i = 0; i < 200; ++i) t.record_interval_us(7000);
  EXPECT_DOUBLE_EQ(t.p50_ms(), 5.0);
  EXPECT_DOUBLE_EQ(t.p99_ms(), 7.0);
}
