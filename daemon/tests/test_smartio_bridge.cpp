#include "smartio_bridge.hpp"

#include "nt_server.hpp"
#include "wpi/nt/IntegerTopic.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <thread>
#include <vector>

using surrogate::ChannelMode;
using surrogate::IGpioBackend;
using surrogate::SmartIoBridge;
using namespace std::chrono_literals;

namespace {

class RecBackend final : public IGpioBackend {
 public:
  enum class Kind { Release, In, Out, Set, Get };
  struct Op {
    Kind kind;
    int gpio;
    bool value;
  };
  std::vector<Op> ops;
  bool input_value = false;

  void release_line(int g) override { ops.push_back({Kind::Release, g, false}); }
  void request_input(int g) override { ops.push_back({Kind::In, g, false}); }
  void request_output(int g, bool v) override { ops.push_back({Kind::Out, g, v}); }
  void set_value(int g, bool v) override { ops.push_back({Kind::Set, g, v}); }
  bool get_value(int) override { return input_value; }
};

constexpr std::array<SmartIoBridge::PinMapping, 2> kTestMap{{
    {0, 17},
    {1, 27},
}};

}  // namespace

TEST(SmartIoBridge, ConstructionWithoutDispatchTouchesNothing) {
  surrogate::NtServer server{56811};
  RecBackend backend;
  SmartIoBridge bridge{server.instance(), backend, kTestMap};
  EXPECT_EQ(bridge.channel_count(), 2u);
  EXPECT_EQ(backend.ops.size(), 0u);
  EXPECT_EQ(bridge.channels_configured(), 0u);
}

TEST(SmartIoBridge, TypePublishedRoutesToBackend) {
  surrogate::NtServer server{56812};
  RecBackend backend;
  SmartIoBridge bridge{server.instance(), backend, kTestMap};

  // Simulate the HAL: publish /io/0/type = 1 (DigitalOutput).
  auto fake_hal_pub = server.instance().GetIntegerTopic("/io/0/type").Publish();
  fake_hal_pub.Set(1);
  std::this_thread::sleep_for(20ms);  // let NT propagate locally
  bridge.tick();

  ASSERT_GE(backend.ops.size(), 1u);
  EXPECT_EQ(backend.ops.back().kind, RecBackend::Kind::Out);
  EXPECT_EQ(backend.ops.back().gpio, 17);
  EXPECT_EQ(bridge.channels_configured(), 1u);
}

TEST(SmartIoBridge, ValsetWhileOutputDrivesBackend) {
  surrogate::NtServer server{56813};
  RecBackend backend;
  SmartIoBridge bridge{server.instance(), backend, kTestMap};

  auto type0 = server.instance().GetIntegerTopic("/io/0/type").Publish();
  auto val0 = server.instance().GetIntegerTopic("/io/0/valset").Publish();
  type0.Set(1);
  std::this_thread::sleep_for(20ms);
  bridge.tick();
  val0.Set(255);
  std::this_thread::sleep_for(20ms);
  bridge.tick();

  bool saw_set_high = false;
  for (auto const& o : backend.ops) {
    if (o.kind == RecBackend::Kind::Set && o.gpio == 17 && o.value) {
      saw_set_high = true;
    }
  }
  EXPECT_TRUE(saw_set_high);
}

TEST(SmartIoBridge, InputPublishesValget) {
  surrogate::NtServer server{56814};
  RecBackend backend;
  backend.input_value = true;
  SmartIoBridge bridge{server.instance(), backend, kTestMap};

  auto type1 = server.instance().GetIntegerTopic("/io/1/type").Publish();
  auto valget_sub = server.instance()
                        .GetIntegerTopic("/io/1/valget")
                        .Subscribe(-999);
  type1.Set(0);  // input mode
  std::this_thread::sleep_for(20ms);
  bridge.tick();
  std::this_thread::sleep_for(20ms);

  // valget should now reflect input_value (255 when high).
  EXPECT_EQ(valget_sub.Get(), 255);
}

TEST(SmartIoBridge, RunningThreadProcessesUpdates) {
  surrogate::NtServer server{56815};
  RecBackend backend;
  SmartIoBridge bridge{server.instance(), backend, kTestMap};
  bridge.start();

  auto type0 = server.instance().GetIntegerTopic("/io/0/type").Publish();
  type0.Set(1);
  // Wait long enough for at least 2 ticks (kTickPeriod=20ms).
  std::this_thread::sleep_for(80ms);
  bridge.stop();

  bool saw = false;
  for (auto const& o : backend.ops) {
    if (o.kind == RecBackend::Kind::Out && o.gpio == 17) saw = true;
  }
  EXPECT_TRUE(saw);
}
