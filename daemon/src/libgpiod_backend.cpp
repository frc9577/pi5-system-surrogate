#include "libgpiod_backend.hpp"

#include <gpiod.hpp>

#include <print>
#include <stdexcept>

namespace surrogate {

namespace {
constexpr const char* kConsumer = "pi5-system-surrogate";
}

LibgpiodBackend::LibgpiodBackend(const std::string& chip_path)
    : chip_{std::make_unique<gpiod::chip>(chip_path)} {
  if (!*chip_) {
    throw std::runtime_error{"libgpiod: failed to open " + chip_path};
  }
}

LibgpiodBackend::~LibgpiodBackend() {
  for (auto& [gpio, line] : lines_) {
    if (line && *line) line->release();
  }
}

void LibgpiodBackend::release_line(int gpio) {
  auto it = lines_.find(gpio);
  if (it == lines_.end()) return;
  if (it->second && *it->second) it->second->release();
  lines_.erase(it);
}

void LibgpiodBackend::request_input(int gpio) {
  release_line(gpio);
  auto line = std::make_unique<gpiod::line>(chip_->get_line(gpio));
  line->request({kConsumer, gpiod::line_request::DIRECTION_INPUT, 0});
  lines_[gpio] = std::move(line);
}

void LibgpiodBackend::request_output(int gpio, bool initial_value) {
  release_line(gpio);
  auto line = std::make_unique<gpiod::line>(chip_->get_line(gpio));
  line->request({kConsumer, gpiod::line_request::DIRECTION_OUTPUT, 0},
                initial_value ? 1 : 0);
  lines_[gpio] = std::move(line);
}

void LibgpiodBackend::set_value(int gpio, bool value) {
  auto it = lines_.find(gpio);
  if (it == lines_.end() || !it->second || !*it->second) return;
  it->second->set_value(value ? 1 : 0);
}

bool LibgpiodBackend::get_value(int gpio) {
  auto it = lines_.find(gpio);
  if (it == lines_.end() || !it->second || !*it->second) return false;
  return it->second->get_value() != 0;
}

}  // namespace surrogate
