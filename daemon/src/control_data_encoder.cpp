#include "control_data_encoder.hpp"

#include "mrc/protobuf/MrcComm.npb.h"
#include "pb_encode.h"

namespace surrogate {

uint32_t pack_control_word(const ControlDataView& v) noexcept {
  uint32_t w = 0;
  if (v.enabled) w |= kCwEnabledBit;
  w |= (static_cast<uint32_t>(v.mode) << kCwRobotModeShift) & kCwRobotModeMask;
  if (v.e_stop) w |= kCwEStopBit;
  if (v.fms_connected) w |= kCwFmsConnectedBit;
  if (v.ds_connected) w |= kCwDsConnectedBit;
  if (v.enabled) w |= kCwWatchdogActiveBit;  // watchdog tracks enable bit
  if (v.supports_opmodes) w |= kCwSupportsOpmodesBit;
  w |= (static_cast<uint32_t>(v.alliance) << kCwAllianceShift) & kCwAllianceMask;
  return w;
}

std::size_t encode_control_data(const ControlDataView& view,
                                std::span<std::byte> out) noexcept {
  if (out.empty()) {
    return 0;
  }
  mrc_proto_ProtobufControlData msg{};
  msg.ControlWord = pack_control_word(view);
  msg.MatchTime = view.match_time_s;
  msg.CurrentOpMode = 0;
  // Joysticks and GameData are pb_callback_t fields; leaving them with no
  // encode callback emits nothing on the wire (empty repeated / empty string).

  pb_ostream_t stream = pb_ostream_from_buffer(
      reinterpret_cast<pb_byte_t*>(out.data()), out.size());
  if (!pb_encode(
          &stream, mrc_proto_ProtobufControlData::msg_descriptor(), &msg)) {
    return 0;
  }
  return stream.bytes_written;
}

}  // namespace surrogate
