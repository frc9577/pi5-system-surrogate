#pragma once

#include "cadence_tracker.hpp"
#include "daemon_state.hpp"
#include "match_controller.hpp"
#include "smartio_bridge.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <string_view>

namespace httplib {
class Server;
}

namespace dssurrogate {

// Single-page web UI + /healthz, served by cpp-httplib on a configurable
// port (default 8080). The page polls /api/state every second and shows
// daemon status; buttons POST to /api/control to drive the DaemonState.
//
// All four endpoint handlers are exposed as pure-ish methods so tests can
// exercise them without an HTTP client.
class WebUI {
 public:
  static constexpr int kDefaultPort = 8080;

  WebUI(DaemonState& state, const CadenceTracker& cadence,
        const SmartIoBridge& smartio, MatchController& match);
  ~WebUI();

  WebUI(const WebUI&) = delete;
  WebUI& operator=(const WebUI&) = delete;

  void start(int port = kDefaultPort);
  void stop();

  // Handlers (public for testing).
  std::string handle_state() const;
  std::string handle_healthz(int* http_status) const;
  void handle_set_control(std::string_view form_body);
  // action ∈ {start, stop, skip-to-teleop}. Unknown actions are no-ops.
  void handle_match(std::string_view form_body);
  static std::string_view root_html();

  std::chrono::seconds uptime() const;

 private:
  DaemonState& state_;
  const CadenceTracker& cadence_;
  const SmartIoBridge& smartio_;
  MatchController& match_;
  std::chrono::steady_clock::time_point start_time_;

  std::unique_ptr<httplib::Server> server_;
  std::thread thread_;
  int bound_port_ = -1;
};

}  // namespace dssurrogate
