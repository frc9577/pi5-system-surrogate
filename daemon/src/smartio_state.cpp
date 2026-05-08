#include "smartio_state.hpp"

namespace dssurrogate {

SmartIoChannel::SmartIoChannel(int channel, int gpio,
                               IGpioBackend& backend) noexcept
    : channel_{channel}, gpio_{gpio}, backend_{backend} {}

void SmartIoChannel::on_type_changed(int hal_type) {
  ChannelMode requested;
  switch (hal_type) {
    case 0:
      requested = ChannelMode::DigitalInput;
      break;
    case 1:
      requested = ChannelMode::DigitalOutput;
      break;
    default:
      // Unsupported mode (PWM/Analog/etc.): release the line if held and
      // drop into Unconfigured. The HAL retries; if the channel ever
      // becomes a supported mode, we configure it then.
      if (mode_ != ChannelMode::Unconfigured) {
        backend_.release_line(gpio_);
        mode_ = ChannelMode::Unconfigured;
      }
      return;
  }

  if (requested == mode_) {
    return;  // idempotent reconfig
  }

  if (mode_ != ChannelMode::Unconfigured) {
    backend_.release_line(gpio_);
  }
  if (requested == ChannelMode::DigitalInput) {
    backend_.request_input(gpio_);
  } else {
    backend_.request_output(gpio_, last_output_);
  }
  mode_ = requested;
}

void SmartIoChannel::on_valset(int hal_value) {
  bool b = (hal_value != 0);
  last_output_ = b;
  if (mode_ != ChannelMode::DigitalOutput) {
    return;  // dropped — HAL re-emits next cycle
  }
  backend_.set_value(gpio_, b);
}

bool SmartIoChannel::sample_input() {
  if (mode_ != ChannelMode::DigitalInput) {
    return false;
  }
  return backend_.get_value(gpio_);
}

}  // namespace dssurrogate
