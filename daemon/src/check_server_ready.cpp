// Tiny NT4 client smoke check. Connects to localhost:6810, verifies the
// two contracts the WPILib SystemCore HAL needs from us at init:
//
//   1. /Netcomm/Control/ServerReady = true
//   2. /Netcomm/Control/ControlData (mrc.proto.ProtobufControlData) with
//      DsConnected=true (else HAL_RefreshDSData zeros the control word)
//
// Exits 0 on full pass, 1 on any timeout/decode failure.

#include "control_data_encoder.hpp"
#include "mrc/protobuf/MrcComm.npb.h"
#include "nt_topics.hpp"
#include "pb_decode.h"
#include "wpi/nt/BooleanTopic.hpp"
#include "wpi/nt/NetworkTableInstance.hpp"
#include "wpi/nt/RawTopic.hpp"

#include <chrono>
#include <print>
#include <thread>
#include <vector>

namespace {

constexpr int kPollMs = 20;
constexpr int kTimeoutMs = 2000;

bool wait_for_server_ready(wpi::nt::NetworkTableInstance& inst) {
  auto sub = inst.GetBooleanTopic(dssurrogate::topics::kServerReady)
                 .Subscribe(false);
  for (int waited = 0; waited <= kTimeoutMs; waited += kPollMs) {
    if (sub.Get()) {
      std::println("ServerReady=true (after {} ms)", waited);
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
  }
  std::println(stderr, "TIMEOUT: ServerReady not received within {} ms",
               kTimeoutMs);
  return false;
}

bool wait_for_control_data(wpi::nt::NetworkTableInstance& inst) {
  auto sub = inst.GetRawTopic(dssurrogate::topics::kControlData)
                 .Subscribe(dssurrogate::topics::kTypeProtoControlData,
                            std::vector<uint8_t>{});
  for (int waited = 0; waited <= kTimeoutMs; waited += kPollMs) {
    auto bytes = sub.Get();
    if (!bytes.empty()) {
      mrc_proto_ProtobufControlData msg{};
      pb_istream_t s = pb_istream_from_buffer(bytes.data(), bytes.size());
      if (!pb_decode(
              &s, mrc_proto_ProtobufControlData::msg_descriptor(), &msg)) {
        std::println(stderr, "ControlData decode failed: {}", PB_GET_ERROR(&s));
        return false;
      }
      if (!(msg.ControlWord & dssurrogate::kCwDsConnectedBit)) {
        std::println(stderr,
                     "ControlData has DsConnected=false; HAL would disable");
        return false;
      }
      std::println("ControlData received ({} bytes, DsConnected=true) after {} ms",
                   bytes.size(), waited);
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
  }
  std::println(stderr, "TIMEOUT: ControlData not received within {} ms",
               kTimeoutMs);
  return false;
}

}  // namespace

int main() {
  auto inst = wpi::nt::NetworkTableInstance::Create();
  inst.SetServer("127.0.0.1", 6810);
  inst.StartClient("check_server_ready");

  bool ok = wait_for_server_ready(inst) && wait_for_control_data(inst);

  inst.StopClient();
  wpi::nt::NetworkTableInstance::Destroy(inst);
  return ok ? 0 : 1;
}
