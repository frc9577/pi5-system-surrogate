#include "smartio_bridge.hpp"

#include "periodic_task.hpp"

#include <format>

namespace dssurrogate {

namespace {
constexpr int64_t kSentinel = -9999;  // outside any valid HAL type/value
}  // namespace

SmartIoBridge::SmartIoBridge(wpi::nt::NetworkTableInstance& inst,
                             IGpioBackend& backend,
                             std::span<const PinMapping> mappings) {
  channels_.reserve(mappings.size());
  for (auto const& m : mappings) {
    auto type_topic = inst.GetIntegerTopic(
        std::format("/io/{}/type", m.channel));
    auto valset_topic = inst.GetIntegerTopic(
        std::format("/io/{}/valset", m.channel));
    auto valget_topic = inst.GetIntegerTopic(
        std::format("/io/{}/valget", m.channel));

    auto glue = std::make_unique<ChannelGlue>(ChannelGlue{
        .channel = SmartIoChannel{m.channel, m.gpio, backend},
        .type_sub = type_topic.Subscribe(kSentinel),
        .valset_sub = valset_topic.Subscribe(kSentinel),
        .valget_pub = valget_topic.Publish(),
        .last_type = kSentinel,
        .last_valset = kSentinel,
    });
    channels_.push_back(std::move(glue));
  }
}

SmartIoBridge::~SmartIoBridge() { stop(); }

void SmartIoBridge::start() {
  thread_ = std::jthread{[this](std::stop_token st) { run(st); }};
}

void SmartIoBridge::stop() {
  if (thread_.joinable()) {
    thread_.request_stop();
    thread_.join();
  }
}

void SmartIoBridge::tick() {
  for (auto& g : channels_) {
    auto t = g->type_sub.Get();
    if (t != g->last_type && t != kSentinel) {
      g->channel.on_type_changed(static_cast<int>(t));
      g->last_type = t;
    }
    auto v = g->valset_sub.Get();
    if (v != g->last_valset && v != kSentinel) {
      g->channel.on_valset(static_cast<int>(v));
      g->last_valset = v;
    }
    if (g->channel.mode() == ChannelMode::DigitalInput) {
      // Publish the sampled input as a 0/255 value mirroring the HAL's
      // SetDigitalOutput convention so the type matches across modes.
      bool hi = g->channel.sample_input();
      g->valget_pub.Set(hi ? 255 : 0);
    }
  }
}

std::size_t SmartIoBridge::channels_configured() const noexcept {
  std::size_t n = 0;
  for (auto const& g : channels_) {
    if (g->channel.mode() != ChannelMode::Unconfigured) ++n;
  }
  return n;
}

void SmartIoBridge::run(std::stop_token st) {
  run_periodic(st, kTickPeriod, [this] { tick(); });
}

}  // namespace dssurrogate
