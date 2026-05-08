#pragma once

namespace dssurrogate {

enum class ChannelMode : int {
  Unconfigured = -1,
  DigitalInput = 0,    // hal type=0
  DigitalOutput = 1,   // hal type=1
  // Pwm/Analog/Counter/AddressableLED — out of scope for first slice.
};

// Abstracted GPIO backend so the SmartIo state machine is unit-testable
// without real libgpiod hardware. Methods are intentionally narrow — just
// what the state machine needs.
class IGpioBackend {
 public:
  virtual ~IGpioBackend() = default;
  virtual void release_line(int gpio) = 0;
  virtual void request_input(int gpio) = 0;
  virtual void request_output(int gpio, bool initial_value) = 0;
  virtual void set_value(int gpio, bool value) = 0;
  virtual bool get_value(int gpio) = 0;
};

// Per-channel state machine. Owns the mode of one libgpiod-backed pin and
// translates HAL `/io/{ch}/type` and `/io/{ch}/valset` updates into
// release/request/set operations on the backend.
//
// Rules (mirror the design doc's derisking section):
//   - on_type_changed releases current line then requests in new mode,
//     atomically per channel
//   - idempotent reconfig (type unchanged) is a no-op
//   - valset arriving while channel isn't an output is dropped silently
//   - last_output is remembered across mode flips so request_output
//     reinstates it
class SmartIoChannel {
 public:
  SmartIoChannel(int channel, int gpio, IGpioBackend& backend) noexcept;

  void on_type_changed(int hal_type);
  void on_valset(int hal_value);
  bool sample_input();

  ChannelMode mode() const noexcept { return mode_; }
  int gpio() const noexcept { return gpio_; }
  int channel() const noexcept { return channel_; }

 private:
  int channel_;
  int gpio_;
  IGpioBackend& backend_;
  ChannelMode mode_ = ChannelMode::Unconfigured;
  bool last_output_ = false;
};

}  // namespace dssurrogate
