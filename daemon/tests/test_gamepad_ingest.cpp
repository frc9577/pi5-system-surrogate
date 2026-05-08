#include "gamepad_ingest.hpp"

#include "daemon_state.hpp"
#include "nt_server.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using surrogate::DaemonState;
using surrogate::GamepadIngest;
using surrogate::RobotMode;
using namespace std::chrono_literals;

TEST(GamepadIngest, EnabledTopicReflectsToDaemonState) {
  surrogate::NtServer server{56820};
  DaemonState state;
  GamepadIngest ingest{server.instance(), state};

  auto pub = server.instance().GetBooleanTopic("/dev/control/enabled").Publish();
  pub.Set(true);
  std::this_thread::sleep_for(20ms);
  ingest.tick();

  EXPECT_TRUE(state.snapshot_control().enabled);
}

TEST(GamepadIngest, ModeIntMapsToEnum) {
  surrogate::NtServer server{56821};
  DaemonState state;
  GamepadIngest ingest{server.instance(), state};

  auto pub = server.instance().GetIntegerTopic("/dev/control/mode").Publish();
  for (auto [in, want] : std::initializer_list<std::pair<int64_t, RobotMode>>{
           {0, RobotMode::Unknown},
           {1, RobotMode::Auto},
           {2, RobotMode::Teleop},
           {3, RobotMode::Test},
           {99, RobotMode::Unknown},
       }) {
    pub.Set(in);
    std::this_thread::sleep_for(15ms);
    ingest.tick();
    EXPECT_EQ(state.snapshot_control().mode, want)
        << "input " << in << " should map to " << static_cast<int>(want);
  }
}

TEST(GamepadIngest, RunLoopAppliesUpdates) {
  surrogate::NtServer server{56823};
  DaemonState state;
  GamepadIngest ingest{server.instance(), state};
  ingest.start();

  auto pub = server.instance().GetBooleanTopic("/dev/control/enabled").Publish();
  pub.Set(true);
  // Ingest tick is 20 ms; 100 ms gives several ticks plus NT propagation.
  std::this_thread::sleep_for(100ms);
  ingest.stop();

  EXPECT_TRUE(state.snapshot_control().enabled);
}

TEST(GamepadIngest, AllianceAndEStopApplied) {
  surrogate::NtServer server{56822};
  DaemonState state;
  GamepadIngest ingest{server.instance(), state};

  auto a_pub = server.instance()
                   .GetIntegerTopic("/dev/control/alliance")
                   .Publish();
  auto e_pub =
      server.instance().GetBooleanTopic("/dev/control/estop").Publish();
  a_pub.Set(2);
  e_pub.Set(true);
  std::this_thread::sleep_for(20ms);
  ingest.tick();

  auto v = state.snapshot_control();
  EXPECT_EQ(v.alliance, 2);
  EXPECT_TRUE(v.e_stop);
}
