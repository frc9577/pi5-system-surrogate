// nt_smartio_test — connects to the daemon as an NT4 client and drives
// /io/{ch}/type and /io/{ch}/valset on a chosen channel. Used by the
// dev-emulation integration test to exercise the SmartIo bridge.
//
// Usage: nt_smartio_test <channel> <type> <valset>
//   channel: 0..5
//   type:    0=DigitalInput, 1=DigitalOutput
//   valset:  0..255 (or any int — nonzero drives high)

#include "wpi/nt/IntegerTopic.hpp"
#include "wpi/nt/NetworkTableInstance.hpp"

#include <chrono>
#include <charconv>
#include <format>
#include <print>
#include <string>
#include <thread>

int main(int argc, char* argv[]) {
  if (argc < 4) {
    std::println(stderr, "usage: {} <channel> <type> <valset>", argv[0]);
    return 2;
  }
  int ch = 0, type = 0, val = 0;
  std::from_chars(argv[1], argv[1] + std::strlen(argv[1]), ch);
  std::from_chars(argv[2], argv[2] + std::strlen(argv[2]), type);
  std::from_chars(argv[3], argv[3] + std::strlen(argv[3]), val);

  auto inst = wpi::nt::NetworkTableInstance::Create();
  inst.SetServer("127.0.0.1", 6810);
  inst.StartClient("nt_smartio_test");

  auto type_pub =
      inst.GetIntegerTopic(std::format("/io/{}/type", ch)).Publish();
  auto val_pub =
      inst.GetIntegerTopic(std::format("/io/{}/valset", ch)).Publish();

  // Hold the publisher long enough for the daemon's 20ms-tick bridge to
  // observe both updates and dispatch them. Two cycles is plenty.
  type_pub.Set(type);
  val_pub.Set(val);
  std::this_thread::sleep_for(std::chrono::milliseconds(80));

  std::println("nt_smartio_test: ch={} type={} valset={}", ch, type, val);

  inst.StopClient();
  wpi::nt::NetworkTableInstance::Destroy(inst);
  return 0;
}
