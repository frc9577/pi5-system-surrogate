#include "control_data_encoder.hpp"

#include "mrc/protobuf/MrcComm.npb.h"
#include "pb_decode.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

using dssurrogate::ControlDataView;
using dssurrogate::encode_control_data;
using dssurrogate::pack_control_word;
using dssurrogate::RobotMode;

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
  EXPECT_EQ(pack_control_word({.enabled = true}) & dssurrogate::kCwEnabledBit,
            dssurrogate::kCwEnabledBit);
  EXPECT_EQ(pack_control_word({.enabled = false}) & dssurrogate::kCwEnabledBit,
            0u);
}

TEST(ControlWordPacking, EnabledAlsoSetsWatchdogActive) {
  // Watchdog tracks `enabled` — it isn't a separate struct field.
  uint32_t cw_on = pack_control_word({.enabled = true});
  uint32_t cw_off = pack_control_word({.enabled = false});
  EXPECT_TRUE(cw_on & dssurrogate::kCwWatchdogActiveBit);
  EXPECT_FALSE(cw_off & dssurrogate::kCwWatchdogActiveBit);
}

TEST(ControlWordPacking, RobotModeAllValues) {
  for (auto m : {RobotMode::Unknown, RobotMode::Auto, RobotMode::Teleop,
                 RobotMode::Test}) {
    auto cw = pack_control_word({.mode = m});
    EXPECT_EQ((cw & dssurrogate::kCwRobotModeMask) >>
                  dssurrogate::kCwRobotModeShift,
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
  EXPECT_TRUE(cw & dssurrogate::kCwEnabledBit);
  EXPECT_TRUE(cw & dssurrogate::kCwDsConnectedBit);
  EXPECT_TRUE(cw & dssurrogate::kCwWatchdogActiveBit);  // derived from enabled
  EXPECT_FALSE(cw & dssurrogate::kCwFmsConnectedBit);
  EXPECT_FALSE(cw & dssurrogate::kCwEStopBit);
  EXPECT_EQ((cw & dssurrogate::kCwRobotModeMask) >>
                dssurrogate::kCwRobotModeShift,
            static_cast<uint32_t>(RobotMode::Teleop));
  EXPECT_EQ((cw & dssurrogate::kCwAllianceMask) >>
                dssurrogate::kCwAllianceShift,
            3u);
}

TEST(ControlWordPacking, AllianceClampedToFourBits) {
  uint32_t cw = pack_control_word({.alliance = 0xFF});
  EXPECT_EQ((cw & dssurrogate::kCwAllianceMask) >>
                dssurrogate::kCwAllianceShift,
            0xFu);
  EXPECT_EQ(cw & ~dssurrogate::kCwAllianceMask, 0u);
}

TEST(ControlDataEncoder, ProducesNonEmptyBytes) {
  std::array<std::byte, dssurrogate::kControlDataMaxBytes> buf{};
  ControlDataView v{.enabled = true, .ds_connected = true};
  std::size_t n = encode_control_data(v, buf);
  EXPECT_GT(n, 0u);
  EXPECT_LE(n, buf.size());
}

TEST(ControlDataEncoder, RoundTripsControlWordAndMatchTime) {
  std::array<std::byte, dssurrogate::kControlDataMaxBytes> buf{};
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
  EXPECT_TRUE(decoded.ControlWord & dssurrogate::kCwEnabledBit);
  EXPECT_TRUE(decoded.ControlWord & dssurrogate::kCwDsConnectedBit);
  EXPECT_EQ((decoded.ControlWord & dssurrogate::kCwRobotModeMask) >>
                dssurrogate::kCwRobotModeShift,
            static_cast<uint32_t>(RobotMode::Auto));
  EXPECT_EQ((decoded.ControlWord & dssurrogate::kCwAllianceMask) >>
                dssurrogate::kCwAllianceShift,
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
