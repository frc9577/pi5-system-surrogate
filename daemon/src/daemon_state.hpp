#pragma once

#include "control_data_encoder.hpp"

#include <mutex>

namespace dssurrogate {

// Single-mutex shared state per the daemon's concurrency model. Every
// bridge (DS, SmartIo, gamepad ingest, web UI) reads/writes through this;
// the periodic publishers snapshot under the lock and encode without it.
//
// Architectural rule: NO I/O, NO LOGGING, NO ALLOCATION, NO SYSCALLS while
// holding the mutex. All public methods are designed to acquire+release
// quickly. Callers obey by snapshotting and operating on the snapshot.
class DaemonState {
 public:
  DaemonState() = default;

  DaemonState(const DaemonState&) = delete;
  DaemonState& operator=(const DaemonState&) = delete;

  void set_enabled(bool v);
  void set_mode(RobotMode m);
  void set_alliance(int a);
  void set_e_stop(bool v);
  void set_match_time(int32_t s);

  // Returns a copy of the fields needed to encode a ProtobufControlData.
  // DsConnected is set unconditionally — the daemon is always alive when
  // this runs, and the HAL zeros the control word otherwise.
  ControlDataView snapshot_control() const;

 private:
  mutable std::mutex mu_;
  bool enabled_ = false;
  RobotMode mode_ = RobotMode::Unknown;
  int alliance_ = 0;
  bool e_stop_ = false;
  int32_t match_time_s_ = 0;
};

}  // namespace dssurrogate
