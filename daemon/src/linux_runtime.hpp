#pragma once

#include <string_view>

namespace dssurrogate {

// Hand-rolled Linux runtime helpers — avoids a libsystemd-dev dep.
//
// All functions are best-effort: they log on failure and return without
// throwing, so a daemon running outside systemd or without privileges
// keeps working.
namespace linux_runtime {

// mlockall(MCL_CURRENT | MCL_FUTURE). Requires CAP_IPC_LOCK or
// LimitMEMLOCK=infinity. Returns true on success.
bool lock_all_memory();

// Standard systemd $NOTIFY_SOCKET payloads — see sd_notify(3).
enum class NotifyKind { Ready, Watchdog, Stopping };

// Notify systemd via $NOTIFY_SOCKET. No-op when the env var isn't set.
void notify(NotifyKind kind);

// Lower-level escape hatch for arbitrary payloads (used by tests).
void notify_raw(std::string_view payload);

}  // namespace linux_runtime

}  // namespace dssurrogate
