#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace surrogate {

enum class RobotMode : int {
  Unknown = 0,
  Auto = 1,
  Teleop = 2,
  Test = 3,
};

// A view of the daemon's current driver-station-equivalent state, packed
// into ControlWord and other fields per FIRSTDriverStation.cpp's layout.
// The Watchdog bit is a derived signal (`enabled`) and lives only in the
// encoded ControlWord, not as a separate struct field.
struct ControlDataView {
  bool enabled = false;
  bool ds_connected = false;
  bool fms_connected = false;
  bool e_stop = false;
  bool supports_opmodes = false;
  RobotMode mode = RobotMode::Unknown;
  int alliance = 0;        // 0..3 on the wire (HAL adds +1 internally)
  int32_t match_time_s = 0;
};

// Bit layout of mrc::ControlData::ControlWord. Mirrors NetComm.h:79-114 in
// the upstream HAL. Public so tests can assert against the same constants
// the encoder uses.
inline constexpr uint32_t kCwEnabledBit         = 1u << 0;
inline constexpr uint32_t kCwRobotModeShift     = 1;
inline constexpr uint32_t kCwRobotModeMask      = 0x3u << kCwRobotModeShift;
inline constexpr uint32_t kCwEStopBit           = 1u << 3;
inline constexpr uint32_t kCwFmsConnectedBit    = 1u << 4;
inline constexpr uint32_t kCwDsConnectedBit     = 1u << 5;
inline constexpr uint32_t kCwWatchdogActiveBit  = 1u << 6;
inline constexpr uint32_t kCwSupportsOpmodesBit = 1u << 7;
inline constexpr uint32_t kCwAllianceShift      = 8;
inline constexpr uint32_t kCwAllianceMask       = 0xFu << kCwAllianceShift;

uint32_t pack_control_word(const ControlDataView& view) noexcept;

// Encode `view` as a `mrc.proto.ProtobufControlData` nanopb wire message
// into `out`. Returns the number of bytes written, or 0 on encoding error.
std::size_t encode_control_data(const ControlDataView& view,
                                std::span<std::byte> out) noexcept;

// Empirically-generous upper bound for our use of ProtobufControlData
// (no joysticks, short GameData). Real ceiling is ~150 bytes; round up.
inline constexpr std::size_t kControlDataMaxBytes = 256;

}  // namespace surrogate
