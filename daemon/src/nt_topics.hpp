#pragma once

#include <string_view>

// Single source of truth for the NT4 topic paths and type-strings the
// daemon serves and consumes. Mirrors the contract in
// docs/daemon-design.md and what the upstream HAL publishes/subscribes.
namespace dssurrogate::topics {

// HAL → us  (we publish)
inline constexpr std::string_view kServerReady = "/Netcomm/Control/ServerReady";
inline constexpr std::string_view kControlData = "/Netcomm/Control/ControlData";
inline constexpr std::string_view kBattery = "/sys/battery";

// Helper / web UI → us  (we subscribe)
inline constexpr std::string_view kCtrlEnabled = "/dev/control/enabled";
inline constexpr std::string_view kCtrlMode = "/dev/control/mode";
inline constexpr std::string_view kCtrlAlliance = "/dev/control/alliance";
inline constexpr std::string_view kCtrlEStop = "/dev/control/estop";

// Diagnostics  (we publish at 1 Hz)
inline constexpr std::string_view kDiagHeartbeat = "/dev/diag/heartbeat";
inline constexpr std::string_view kDiagP99 = "/dev/diag/cadence_p99_ms";
inline constexpr std::string_view kDiagP50 = "/dev/diag/cadence_p50_ms";
inline constexpr std::string_view kDiagUptime = "/dev/diag/uptime_s";
inline constexpr std::string_view kDiagBuildInfo = "/dev/diag/build_info";

// Type-strings (raw-topic schema identifiers)
inline constexpr std::string_view kTypeProtoControlData =
    "proto:mrc.proto.ProtobufControlData";

}  // namespace dssurrogate::topics
