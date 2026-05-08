#include "daemon_state.hpp"

namespace surrogate {

void DaemonState::set_enabled(bool v) {
  std::lock_guard lk{mu_};
  enabled_ = v;
}

void DaemonState::set_mode(RobotMode m) {
  std::lock_guard lk{mu_};
  mode_ = m;
}

void DaemonState::set_alliance(int a) {
  std::lock_guard lk{mu_};
  alliance_ = a;
}

void DaemonState::set_e_stop(bool v) {
  std::lock_guard lk{mu_};
  e_stop_ = v;
}

void DaemonState::set_match_time(int32_t s) {
  std::lock_guard lk{mu_};
  match_time_s_ = s;
}

ControlDataView DaemonState::snapshot_control() const {
  std::lock_guard lk{mu_};
  return ControlDataView{
      .enabled = enabled_,
      .ds_connected = true,
      .fms_connected = false,
      .e_stop = e_stop_,
      .supports_opmodes = false,
      .mode = mode_,
      .alliance = alliance_,
      .match_time_s = match_time_s_,
  };
}

}  // namespace surrogate
