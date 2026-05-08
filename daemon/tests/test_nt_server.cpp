#include "nt_server.hpp"

#include "wpi/nt/BooleanTopic.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using dssurrogate::NtServer;

namespace {

// Use a port well outside any plausible system service range so a running
// production daemon on 6810 doesn't collide with the test.
constexpr unsigned int kTestPort = 56810;

}  // namespace

TEST(NtServer, ConstructAndDestroyDoesNotThrow) {
  NtServer s{kTestPort};
  EXPECT_EQ(s.port(), kTestPort);
}

TEST(NtServer, ExposesNetworkTableInstanceForPublish) {
  NtServer s{kTestPort};
  // Trivially exercise the instance; if the underlying NT handle were
  // invalid, GetBooleanTopic would crash.
  auto pub = s.instance()
                 .GetBooleanTopic("/test/nt_server/ready")
                 .Publish();
  pub.Set(true);
  // Give ntcore a moment to enqueue. We don't subscribe back from the
  // same instance — just verifying no crash through publish path.
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  SUCCEED();
}

TEST(NtServer, SequentialServersOnDifferentPortsCoexist) {
  NtServer a{kTestPort};
  NtServer b{kTestPort + 1};
  EXPECT_EQ(a.port(), kTestPort);
  EXPECT_EQ(b.port(), kTestPort + 1);
}
