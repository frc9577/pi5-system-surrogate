#include "control_data_publisher.hpp"

#include "control_data_encoder.hpp"
#include "daemon_state.hpp"
#include "mrc/protobuf/MrcComm.npb.h"
#include "pb_decode.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

using dssurrogate::ControlDataPublisher;
using dssurrogate::DaemonState;
using dssurrogate::RobotMode;
using namespace std::chrono_literals;

namespace {

struct CapturingSink {
  std::mutex mu;
  std::vector<std::vector<std::byte>> received;

  ControlDataPublisher::ByteSink make_sink() {
    return [this](std::span<const std::byte> bytes) {
      std::lock_guard lk{mu};
      received.emplace_back(bytes.begin(), bytes.end());
    };
  }

  std::size_t size() {
    std::lock_guard lk{mu};
    return received.size();
  }
};

}  // namespace

TEST(ControlDataPublisher, EmitsPeriodicBytesAndStopsCleanly) {
  DaemonState state;
  state.set_enabled(true);
  CapturingSink sink;

  ControlDataPublisher pub{state, sink.make_sink()};
  pub.start(5ms);
  std::this_thread::sleep_for(50ms);
  pub.stop();

  std::lock_guard lk{sink.mu};
  EXPECT_GE(sink.received.size(), 5u);
  EXPECT_LE(sink.received.size(), 25u);
  for (auto const& v : sink.received) {
    EXPECT_GT(v.size(), 0u);
  }
}

TEST(ControlDataPublisher, ReflectsLiveStateChanges) {
  DaemonState state;
  CapturingSink sink;
  ControlDataPublisher pub{state, sink.make_sink()};
  pub.start(5ms);

  std::this_thread::sleep_for(15ms);
  state.set_enabled(true);
  state.set_mode(RobotMode::Auto);
  std::this_thread::sleep_for(30ms);
  pub.stop();

  std::lock_guard lk{sink.mu};
  ASSERT_GE(sink.received.size(), 4u);

  // First few snapshots: enabled=false. Last few: enabled=true.
  auto decode_enabled = [](std::span<const std::byte> bytes) {
    mrc_proto_ProtobufControlData out{};
    pb_istream_t s = pb_istream_from_buffer(
        reinterpret_cast<const pb_byte_t*>(bytes.data()), bytes.size());
    EXPECT_TRUE(pb_decode(
        &s, mrc_proto_ProtobufControlData::msg_descriptor(), &out));
    return (out.ControlWord & dssurrogate::kCwEnabledBit) != 0;
  };
  EXPECT_FALSE(decode_enabled(sink.received.front()));
  EXPECT_TRUE(decode_enabled(sink.received.back()));
}

TEST(ControlDataPublisher, CadenceStatsPopulatedAfterRun) {
  DaemonState state;
  CapturingSink sink;
  ControlDataPublisher pub{state, sink.make_sink()};
  pub.start(5ms);
  std::this_thread::sleep_for(60ms);
  pub.stop();

  EXPECT_GT(pub.cadence().sample_count(), 5u);
  // We can't assert tight bounds on a normal-priority thread, but p50
  // should land within an order of magnitude of 5 ms.
  EXPECT_GT(pub.cadence().p50_ms(), 1.0);
  EXPECT_LT(pub.cadence().p50_ms(), 50.0);
}

TEST(ControlDataPublisher, DestructorJoinsCleanly) {
  DaemonState state;
  CapturingSink sink;
  {
    ControlDataPublisher pub{state, sink.make_sink()};
    pub.start(5ms);
    std::this_thread::sleep_for(15ms);
  }  // ~ destructor joins
  EXPECT_GT(sink.size(), 0u);
}
