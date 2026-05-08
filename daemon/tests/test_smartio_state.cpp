#include "smartio_state.hpp"

#include <gtest/gtest.h>

#include <random>
#include <vector>

using dssurrogate::ChannelMode;
using dssurrogate::IGpioBackend;
using dssurrogate::SmartIoChannel;

namespace {

class RecordingBackend final : public IGpioBackend {
 public:
  enum class Kind { Release, RequestIn, RequestOut, SetValue, GetValue };
  struct Op {
    Kind kind;
    int gpio;
    bool value;
  };
  std::vector<Op> ops;
  bool input_value = false;

  void release_line(int g) override { ops.push_back({Kind::Release, g, false}); }
  void request_input(int g) override { ops.push_back({Kind::RequestIn, g, false}); }
  void request_output(int g, bool v) override {
    ops.push_back({Kind::RequestOut, g, v});
  }
  void set_value(int g, bool v) override { ops.push_back({Kind::SetValue, g, v}); }
  bool get_value(int g) override {
    ops.push_back({Kind::GetValue, g, false});
    return input_value;
  }
};

}  // namespace

TEST(SmartIo, StartsUnconfiguredAndCallsNothing) {
  RecordingBackend b;
  SmartIoChannel ch{0, 17, b};
  EXPECT_EQ(ch.mode(), ChannelMode::Unconfigured);
  EXPECT_TRUE(b.ops.empty());
}

TEST(SmartIo, ConfigureInputCallsRequestInput) {
  RecordingBackend b;
  SmartIoChannel ch{0, 17, b};
  ch.on_type_changed(0);
  ASSERT_EQ(b.ops.size(), 1u);
  EXPECT_EQ(b.ops[0].kind, RecordingBackend::Kind::RequestIn);
  EXPECT_EQ(b.ops[0].gpio, 17);
  EXPECT_EQ(ch.mode(), ChannelMode::DigitalInput);
}

TEST(SmartIo, ConfigureOutputCallsRequestOutputLow) {
  RecordingBackend b;
  SmartIoChannel ch{0, 17, b};
  ch.on_type_changed(1);
  ASSERT_EQ(b.ops.size(), 1u);
  EXPECT_EQ(b.ops[0].kind, RecordingBackend::Kind::RequestOut);
  EXPECT_EQ(b.ops[0].gpio, 17);
  EXPECT_FALSE(b.ops[0].value);
  EXPECT_EQ(ch.mode(), ChannelMode::DigitalOutput);
}

TEST(SmartIo, FlipFromInputToOutputReleasesThenRequests) {
  RecordingBackend b;
  SmartIoChannel ch{0, 17, b};
  ch.on_type_changed(0);
  ch.on_type_changed(1);
  ASSERT_EQ(b.ops.size(), 3u);
  EXPECT_EQ(b.ops[0].kind, RecordingBackend::Kind::RequestIn);
  EXPECT_EQ(b.ops[1].kind, RecordingBackend::Kind::Release);
  EXPECT_EQ(b.ops[2].kind, RecordingBackend::Kind::RequestOut);
}

TEST(SmartIo, IdempotentReconfigSkipsBackend) {
  RecordingBackend b;
  SmartIoChannel ch{0, 17, b};
  ch.on_type_changed(0);
  ch.on_type_changed(0);
  EXPECT_EQ(b.ops.size(), 1u);
}

TEST(SmartIo, ValsetWhileOutputDrivesBackend) {
  RecordingBackend b;
  SmartIoChannel ch{0, 17, b};
  ch.on_type_changed(1);
  ch.on_valset(255);
  ASSERT_EQ(b.ops.size(), 2u);
  EXPECT_EQ(b.ops[1].kind, RecordingBackend::Kind::SetValue);
  EXPECT_TRUE(b.ops[1].value);
  ch.on_valset(0);
  ASSERT_EQ(b.ops.size(), 3u);
  EXPECT_FALSE(b.ops[2].value);
}

TEST(SmartIo, ValsetWhileInputIsDropped) {
  RecordingBackend b;
  SmartIoChannel ch{0, 17, b};
  ch.on_type_changed(0);
  ch.on_valset(255);
  EXPECT_EQ(b.ops.size(), 1u);
}

TEST(SmartIo, ValsetWhileUnconfiguredIsDropped) {
  RecordingBackend b;
  SmartIoChannel ch{0, 17, b};
  ch.on_valset(255);
  EXPECT_TRUE(b.ops.empty());
}

TEST(SmartIo, UnsupportedTypeReleasesAndDropsToUnconfigured) {
  RecordingBackend b;
  SmartIoChannel ch{0, 17, b};
  ch.on_type_changed(0);
  ch.on_type_changed(99);  // PWM / AddressableLED / Counter — out of scope
  EXPECT_EQ(ch.mode(), ChannelMode::Unconfigured);
  ASSERT_EQ(b.ops.size(), 2u);
  EXPECT_EQ(b.ops[1].kind, RecordingBackend::Kind::Release);
}

TEST(SmartIo, UnsupportedTypeFromUnconfiguredIsNoOp) {
  RecordingBackend b;
  SmartIoChannel ch{0, 17, b};
  ch.on_type_changed(13);  // AddressableLED
  EXPECT_TRUE(b.ops.empty());
  EXPECT_EQ(ch.mode(), ChannelMode::Unconfigured);
}

TEST(SmartIo, SampleInputReadsBackendOnlyWhenInput) {
  RecordingBackend b;
  SmartIoChannel ch{0, 17, b};
  EXPECT_FALSE(ch.sample_input());
  EXPECT_TRUE(b.ops.empty());

  ch.on_type_changed(0);
  b.input_value = true;
  EXPECT_TRUE(ch.sample_input());

  b.input_value = false;
  EXPECT_FALSE(ch.sample_input());

  ch.on_type_changed(1);
  EXPECT_FALSE(ch.sample_input());  // not an input — short-circuits
}

TEST(SmartIo, LastOutputRememberedAcrossReconfig) {
  RecordingBackend b;
  SmartIoChannel ch{0, 17, b};
  ch.on_type_changed(1);
  ch.on_valset(255);   // output goes high
  ch.on_type_changed(0);
  ch.on_type_changed(1);
  ASSERT_EQ(b.ops.back().kind, RecordingBackend::Kind::RequestOut);
  EXPECT_TRUE(b.ops.back().value)
      << "request_output must reinstate last commanded value";
}

TEST(SmartIo, FuzzRandomSequencesNeverCrashAndStayConsistent) {
  // Drive many random (type, valset) sequences. Verify backend invariants:
  //   - ops are well-formed (correct gpio)
  //   - the channel never claims a mode without a backing request
  RecordingBackend b;
  SmartIoChannel ch{2, 22, b};
  std::mt19937 rng{42};
  std::uniform_int_distribution<int> op_dist{0, 2};
  std::uniform_int_distribution<int> type_dist{-1, 14};
  std::uniform_int_distribution<int> val_dist{0, 1};

  int requests_seen = 0;
  int releases_seen = 0;

  for (int i = 0; i < 5'000; ++i) {
    int op = op_dist(rng);
    if (op == 0) {
      ch.on_type_changed(type_dist(rng));
    } else if (op == 1) {
      ch.on_valset(val_dist(rng) * 255);
    } else {
      (void)ch.sample_input();
    }
    for (auto const& o : b.ops) {
      ASSERT_EQ(o.gpio, 22);
    }
    requests_seen = 0;
    releases_seen = 0;
    for (auto const& o : b.ops) {
      if (o.kind == RecordingBackend::Kind::RequestIn ||
          o.kind == RecordingBackend::Kind::RequestOut) {
        ++requests_seen;
      } else if (o.kind == RecordingBackend::Kind::Release) {
        ++releases_seen;
      }
    }
    // requests = releases + (1 if currently configured else 0)
    int currently_held = (ch.mode() == ChannelMode::Unconfigured) ? 0 : 1;
    EXPECT_EQ(requests_seen - releases_seen, currently_held)
        << "leaked or double-released a libgpiod line at iter " << i;
  }
}
