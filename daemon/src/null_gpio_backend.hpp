#pragma once

#include "smartio_state.hpp"

namespace dssurrogate {

// No-op backend: used when libgpiod can't open the chip (e.g., running
// the daemon on a workstation for development). All operations are
// silent except input reads, which always return false.
class NullGpioBackend final : public IGpioBackend {
 public:
  void release_line(int) override {}
  void request_input(int) override {}
  void request_output(int, bool) override {}
  void set_value(int, bool) override {}
  bool get_value(int) override { return false; }
};

}  // namespace dssurrogate
