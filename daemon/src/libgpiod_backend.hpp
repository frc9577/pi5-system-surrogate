#pragma once

#include "smartio_state.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace gpiod {
class chip;
class line;
}

namespace surrogate {

// libgpiod v1 backend. Constructor throws if the chip can't be opened.
// Each requested GPIO is held in a map until release_line is called.
class LibgpiodBackend final : public IGpioBackend {
 public:
  explicit LibgpiodBackend(const std::string& chip_path = "/dev/gpiochip0");
  ~LibgpiodBackend() override;

  LibgpiodBackend(const LibgpiodBackend&) = delete;
  LibgpiodBackend& operator=(const LibgpiodBackend&) = delete;

  void release_line(int gpio) override;
  void request_input(int gpio) override;
  void request_output(int gpio, bool initial_value) override;
  void set_value(int gpio, bool value) override;
  bool get_value(int gpio) override;

 private:
  std::unique_ptr<gpiod::chip> chip_;
  std::unordered_map<int, std::unique_ptr<gpiod::line>> lines_;
};

}  // namespace surrogate
