#include "control_data_encoder.hpp"

#include "mrc/protobuf/MrcComm.npb.h"
#include "pb_decode.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

using surrogate::ControlDataView;
using surrogate::encode_control_data;
using surrogate::pack_control_word;
using surrogate::RobotMode;

namespace {

mrc_proto_ProtobufControlData decode(std::span<const std::byte> bytes) {
  mrc_proto_ProtobufControlData out{};
  pb_istream_t stream = pb_istream_from_buffer(
      reinterpret_cast<const pb_byte_t*>(bytes.data()), bytes.size());
  EXPECT_TRUE(pb_decode(
      &stream, mrc_proto_ProtobufControlData::msg_descriptor(), &out))
      << "nanopb decode failed: " << PB_GET_ERROR(&stream);
  return out;
}

}  // namespace

TEST(ControlWordPacking, EnabledBit) {
  EXPECT_EQ(pack_control_word({.enabled = true}) & surrogate::kCwEnabledBit,
            surrogate::kCwEnabledBit);
  EXPECT_EQ(pack_control_word({.enabled = false}) & surrogate::kCwEnabledBit,
            0u);
}

TEST(ControlWordPacking, EnabledAlsoSetsWatchdogActive) {
  // Watchdog tracks `enabled` — it isn't a separate struct field.
  uint32_t cw_on = pack_control_word({.enabled = true});
  uint32_t cw_off = pack_control_word({.enabled = false});
  EXPECT_TRUE(cw_on & surrogate::kCwWatchdogActiveBit);
  EXPECT_FALSE(cw_off & surrogate::kCwWatchdogActiveBit);
}

TEST(ControlWordPacking, RobotModeAllValues) {
  for (auto m : {RobotMode::Unknown, RobotMode::Auto, RobotMode::Teleop,
                 RobotMode::Test}) {
    auto cw = pack_control_word({.mode = m});
    EXPECT_EQ((cw & surrogate::kCwRobotModeMask) >>
                  surrogate::kCwRobotModeShift,
              static_cast<uint32_t>(m));
  }
}

TEST(ControlWordPacking, IndependentFlagsDoNotInterfere) {
  ControlDataView v{
      .enabled = true,
      .ds_connected = true,
      .mode = RobotMode::Teleop,
      .alliance = 3,
  };
  uint32_t cw = pack_control_word(v);
  EXPECT_TRUE(cw & surrogate::kCwEnabledBit);
  EXPECT_TRUE(cw & surrogate::kCwDsConnectedBit);
  EXPECT_TRUE(cw & surrogate::kCwWatchdogActiveBit);  // derived from enabled
  EXPECT_FALSE(cw & surrogate::kCwFmsConnectedBit);
  EXPECT_FALSE(cw & surrogate::kCwEStopBit);
  EXPECT_EQ((cw & surrogate::kCwRobotModeMask) >>
                surrogate::kCwRobotModeShift,
            static_cast<uint32_t>(RobotMode::Teleop));
  EXPECT_EQ((cw & surrogate::kCwAllianceMask) >>
                surrogate::kCwAllianceShift,
            3u);
}

TEST(ControlWordPacking, AllianceClampedToFourBits) {
  uint32_t cw = pack_control_word({.alliance = 0xFF});
  EXPECT_EQ((cw & surrogate::kCwAllianceMask) >>
                surrogate::kCwAllianceShift,
            0xFu);
  EXPECT_EQ(cw & ~surrogate::kCwAllianceMask, 0u);
}

TEST(ControlDataEncoder, ProducesNonEmptyBytes) {
  std::array<std::byte, surrogate::kControlDataMaxBytes> buf{};
  ControlDataView v{.enabled = true, .ds_connected = true};
  std::size_t n = encode_control_data(v, buf);
  EXPECT_GT(n, 0u);
  EXPECT_LE(n, buf.size());
}

TEST(ControlDataEncoder, RoundTripsControlWordAndMatchTime) {
  std::array<std::byte, surrogate::kControlDataMaxBytes> buf{};
  ControlDataView v{
      .enabled = true,
      .ds_connected = true,
      .mode = RobotMode::Auto,
      .alliance = 2,
      .match_time_s = 47,
  };
  std::size_t n = encode_control_data(v, buf);
  ASSERT_GT(n, 0u);

  auto decoded = decode(std::span<const std::byte>{buf.data(), n});
  EXPECT_EQ(decoded.MatchTime, 47);
  EXPECT_TRUE(decoded.ControlWord & surrogate::kCwEnabledBit);
  EXPECT_TRUE(decoded.ControlWord & surrogate::kCwDsConnectedBit);
  EXPECT_EQ((decoded.ControlWord & surrogate::kCwRobotModeMask) >>
                surrogate::kCwRobotModeShift,
            static_cast<uint32_t>(RobotMode::Auto));
  EXPECT_EQ((decoded.ControlWord & surrogate::kCwAllianceMask) >>
                surrogate::kCwAllianceShift,
            2u);
}

TEST(ControlDataEncoder, ZeroBufferReportsZero) {
  std::array<std::byte, 0> buf{};
  ControlDataView v{.enabled = true};
  EXPECT_EQ(encode_control_data(v, buf), 0u);
}

TEST(ControlDataEncoder, TooSmallBufferReturnsZero) {
  // A non-empty but undersized buffer forces pb_encode to fail mid-write.
  // MatchTime at INT32_MAX takes 5 bytes for the varint tag+payload, plus
  // ~2 bytes for the ControlWord — well over 3 bytes total.
  std::array<std::byte, 3> buf{};
  ControlDataView v{
      .enabled = true,
      .ds_connected = true,
      .match_time_s = 2'147'483'647,
  };
  EXPECT_EQ(encode_control_data(v, buf), 0u);
}
