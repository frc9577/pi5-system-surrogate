// Smoke test — proves the GoogleTest harness builds, links against
// daemon_core, and runs. If this fails, no other test will help.

#include <gtest/gtest.h>

#include "nt_server.hpp"

TEST(Smoke, ArithmeticSanity) {
  EXPECT_EQ(2 + 2, 4);
}

TEST(Smoke, NtServerHeaderIsReachable) {
  // Just compile-time: the header is on the include path and the type exists.
  static_assert(!std::is_default_constructible_v<surrogate::NtServer>
                || std::is_default_constructible_v<surrogate::NtServer>);
  SUCCEED();
}
