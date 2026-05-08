// ds-surrogate daemon: NT4 server on localhost:6810 that stands in for
// Limelight's system-server. See docs/daemon-design.md.

#include "control_data_publisher.hpp"
#include "daemon_state.hpp"
#include "diagnostics_publisher.hpp"
#include "gamepad_ingest.hpp"
#include "imu_publisher.hpp"
#include "libgpiod_backend.hpp"
#include "linux_runtime.hpp"
#include "match_controller.hpp"
#include "null_gpio_backend.hpp"
#include "nt_server.hpp"
#include "nt_topics.hpp"
#include "rsl_blinker.hpp"
#include "smartio_bridge.hpp"
#include "smartio_state.hpp"
#include "web_ui.hpp"

#include "wpi/nt/BooleanTopic.hpp"
#include "wpi/nt/DoubleTopic.hpp"
#include "wpi/nt/RawTopic.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <memory>
#include <print>
#include <span>
#include <string>
#include <thread>

namespace {

std::atomic<bool> stop_requested{false};

extern "C" void on_signal(int /*signo*/) noexcept {
  stop_requested.store(true, std::memory_order_relaxed);
}

// Pin map per docs/daemon-design.md. Channels 0..3 wired; 4..5 reserved.
constexpr std::array<dssurrogate::SmartIoBridge::PinMapping, 4> kPinMap{{
    {0, 17},
    {1, 27},
    {2, 22},
    {3, 23},
}};

constexpr int kRslGpio = 26;

}  // namespace

int main() {
  std::signal(SIGTERM, on_signal);
  std::signal(SIGINT, on_signal);

  using namespace std::chrono_literals;
  using dssurrogate::linux_runtime::lock_all_memory;
  using dssurrogate::linux_runtime::notify;
  using dssurrogate::linux_runtime::NotifyKind;
  namespace topics = dssurrogate::topics;

  // Avoid page-fault stalls in the publisher. Ignored failure (e.g., when
  // not running as root or without LimitMEMLOCK=infinity).
  (void)lock_all_memory();

  dssurrogate::NtServer server;
  auto& inst = server.instance();

  // Order matters: ServerReady must appear BEFORE the HAL connects, or
  // HAL_Initialize() polls for it for 10s and then std::terminate()s.
  auto server_ready_pub = inst.GetBooleanTopic(topics::kServerReady).Publish();
  server_ready_pub.Set(true);

  auto battery_pub = inst.GetDoubleTopic(topics::kBattery).Publish();
  battery_pub.Set(12.0);

  dssurrogate::ImuPublisher imu{inst};

  auto control_data_pub = inst.GetRawTopic(topics::kControlData)
                              .Publish(topics::kTypeProtoControlData);

  dssurrogate::DaemonState state;

  std::unique_ptr<dssurrogate::IGpioBackend> backend;
  std::string chip_path = "/dev/gpiochip0";
  if (const char* override_path = std::getenv("DS_SURROGATE_GPIOCHIP");
      override_path != nullptr && override_path[0] != '\0') {
    chip_path = override_path;
  }
  try {
    backend = std::make_unique<dssurrogate::LibgpiodBackend>(chip_path);
    std::println("ds-surrogate: libgpiod backend on {}", chip_path);
  } catch (std::exception const& e) {
    std::println(stderr,
                 "ds-surrogate: libgpiod unavailable ({}) — using null backend",
                 e.what());
    backend = std::make_unique<dssurrogate::NullGpioBackend>();
  }

  dssurrogate::ControlDataPublisher publisher{
      state, [&control_data_pub](std::span<const std::byte> bytes) {
        control_data_pub.Set(std::span<const uint8_t>{
            reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()});
      }};
  publisher.start(20ms);

  dssurrogate::SmartIoBridge smartio{inst, *backend, kPinMap};
  smartio.start();

  dssurrogate::RslBlinker rsl{state, *backend, kRslGpio};
  rsl.start();

  dssurrogate::GamepadIngest gamepad{inst, state};
  gamepad.start();

  dssurrogate::DiagnosticsPublisher diagnostics{
      inst, publisher.cadence(),
      "ds-surrogate dev " __DATE__ " " __TIME__ " g++-16"};
  diagnostics.start();

  dssurrogate::MatchController match{state};
  match.start();

  dssurrogate::WebUI ui{state, publisher.cadence(), smartio, match};
  ui.start();

  notify(NotifyKind::Ready);
  std::println("ds-surrogate: ready (NT4 :6810, web :8080)");

  // Watchdog tick — also serves as our main idle loop.
  using clock = std::chrono::steady_clock;
  auto next_watchdog = clock::now() + 1s;
  while (!stop_requested.load(std::memory_order_relaxed)) {
    std::this_thread::sleep_for(100ms);
    if (clock::now() >= next_watchdog) {
      notify(NotifyKind::Watchdog);
      next_watchdog += 1s;
    }
  }

  notify(NotifyKind::Stopping);
  std::println("ds-surrogate: shutting down (cadence p50={:.2f} p99={:.2f} ms)",
               publisher.cadence().p50_ms(), publisher.cadence().p99_ms());

  ui.stop();
  match.stop();
  diagnostics.stop();
  gamepad.stop();
  rsl.stop();
  smartio.stop();
  publisher.stop();
  return 0;
}
