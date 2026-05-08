#include "web_ui.hpp"

#include "control_data_encoder.hpp"
#include "httplib.h"

#include <charconv>
#include <format>
#include <print>

namespace dssurrogate {

namespace {

constexpr std::string_view kRootHtml = R"HTML(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>ds-surrogate</title>
<style>
body { font-family: ui-monospace, monospace; background: #1a1a1a; color: #e0e0e0; padding: 2em; max-width: 720px; }
h1 { color: #4caf50; }
.row { margin: 0.4em 0; }
.k { color: #888; display: inline-block; width: 14em; }
.v { color: #4caf50; }
.bad { color: #f44336; }
.warn { color: #ffa726; }
button { background: #4caf50; color: white; border: 0; padding: 0.5em 1em; margin: 0.3em 0.3em 0.3em 0; cursor: pointer; font-family: inherit; }
button.off { background: #555; }
button.estop { background: #f44336; }
hr { border: 0; border-top: 1px solid #333; margin: 2em 0; }
</style>
</head>
<body>
<h1>ds-surrogate</h1>
<div id="content">loading...</div>
<hr>
<h2>Match</h2>
<div>
  <button onclick="postMatch('start')">Start Match</button>
  <button class="off" onclick="postMatch('stop')">Stop Match</button>
  <button onclick="postMatch('skip-to-teleop')">Skip to Teleop</button>
</div>
<h2>Manual override (use only when match phase is idle)</h2>
<div>
  <button onclick="setControl({enabled: true})">Enable</button>
  <button class="off" onclick="setControl({enabled: false})">Disable</button>
  <button onclick="setControl({mode: 1})">Auto</button>
  <button onclick="setControl({mode: 2})">Teleop</button>
  <button onclick="setControl({mode: 3})">Test</button>
  <button class="estop" onclick="setControl({estop: true})">E-Stop</button>
  <button class="off" onclick="setControl({estop: false})">Clear E-Stop</button>
</div>
<script>
async function refresh() {
  try {
    const r = await fetch('/api/state');
    if (!r.ok) throw new Error('http ' + r.status);
    const s = await r.json();
    const cls = (cond, badClass) => cond ? `class="${badClass}"` : 'class="v"';
    document.getElementById('content').innerHTML =
      `<div class="row"><span class="k">match phase</span> <span class="v">${s.match.phase}</span></div>` +
      `<div class="row"><span class="k">match elapsed</span> <span class="v">${s.match.total_elapsed_s.toFixed(1)} s</span></div>` +
      `<div class="row"><span class="k">phase remaining</span> <span class="v">${s.match.remaining_s.toFixed(1)} s</span></div>` +
      `<div class="row"><span class="k">enabled</span> <span ${cls(!s.control.enabled, 'warn')}>${s.control.enabled}</span></div>` +
      `<div class="row"><span class="k">mode</span> <span class="v">${s.control.mode}</span></div>` +
      `<div class="row"><span class="k">alliance</span> <span class="v">${s.control.alliance}</span></div>` +
      `<div class="row"><span class="k">e-stop</span> <span ${cls(s.control.estop, 'bad')}>${s.control.estop}</span></div>` +
      `<div class="row"><span class="k">smartio configured</span> <span class="v">${s.smartio.channels_configured} / ${s.smartio.channel_count}</span></div>` +
      `<div class="row"><span class="k">controldata p50</span> <span ${cls(s.cadence.p50_ms > 25, 'warn')}>${s.cadence.p50_ms.toFixed(2)} ms</span></div>` +
      `<div class="row"><span class="k">controldata p99</span> <span ${cls(s.cadence.p99_ms > 25, 'warn')}>${s.cadence.p99_ms.toFixed(2)} ms</span></div>` +
      `<div class="row"><span class="k">cadence samples</span> <span class="v">${s.cadence.samples}</span></div>` +
      `<div class="row"><span class="k">uptime</span> <span class="v">${s.uptime_s.toFixed(1)} s</span></div>`;
  } catch (e) {
    document.getElementById('content').innerHTML = '<div class="bad">disconnected</div>';
  }
}
async function setControl(v) {
  const body = new URLSearchParams(v).toString();
  await fetch('/api/control', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body });
  refresh();
}
async function postMatch(action) {
  const body = new URLSearchParams({action}).toString();
  await fetch('/api/match', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body });
  refresh();
}
refresh();
setInterval(refresh, 1000);
</script>
</body>
</html>)HTML";

const char* mode_name(RobotMode m) noexcept {
  switch (m) {
    case RobotMode::Auto: return "auto";
    case RobotMode::Teleop: return "teleop";
    case RobotMode::Test: return "test";
    case RobotMode::Unknown: return "unknown";
  }
  return "unknown";
}

}  // namespace

WebUI::WebUI(DaemonState& state, const CadenceTracker& cadence,
             const SmartIoBridge& smartio, MatchController& match)
    : state_{state},
      cadence_{cadence},
      smartio_{smartio},
      match_{match},
      start_time_{std::chrono::steady_clock::now()} {}

WebUI::~WebUI() { stop(); }

std::string_view WebUI::root_html() { return kRootHtml; }

std::chrono::seconds WebUI::uptime() const {
  return std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now() - start_time_);
}

std::string WebUI::handle_state() const {
  auto v = state_.snapshot_control();
  auto up = std::chrono::duration_cast<std::chrono::duration<double>>(
                std::chrono::steady_clock::now() - start_time_)
                .count();
  auto m = match_.snapshot();
  return std::format(
      R"({{"control":{{"enabled":{},"mode":"{}","alliance":{},"estop":{}}},)"
      R"("match":{{"phase":"{}","total_elapsed_s":{:.1f},"remaining_s":{:.1f}}},)"
      R"("smartio":{{"channels_configured":{},"channel_count":{}}},)"
      R"("cadence":{{"p50_ms":{:.3f},"p99_ms":{:.3f},"samples":{}}},)"
      R"("uptime_s":{:.3f}}})",
      v.enabled ? "true" : "false", mode_name(v.mode), v.alliance,
      v.e_stop ? "true" : "false", match_phase_name(m.phase),
      m.total_elapsed.count() / 1000.0, m.remaining_in_phase.count() / 1000.0,
      smartio_.channels_configured(), smartio_.channel_count(),
      cadence_.p50_ms(), cadence_.p99_ms(), cadence_.sample_count(), up);
}

std::string WebUI::handle_healthz(int* http_status) const {
  bool healthy = cadence_.p99_ms() < 25.0;
  if (http_status) *http_status = healthy ? 200 : 503;
  return std::format(
      R"({{"status":"{}","cadence_p99_ms":{:.3f},"uptime_s":{:.3f}}})",
      healthy ? "ok" : "degraded", cadence_.p99_ms(),
      std::chrono::duration_cast<std::chrono::duration<double>>(
          std::chrono::steady_clock::now() - start_time_)
          .count());
}

void WebUI::handle_match(std::string_view body) {
  // body is form-encoded "action=<verb>". Unknown verbs are no-ops.
  auto pos = body.find("action=");
  if (pos == std::string_view::npos) return;
  auto start = pos + 7;
  auto end = body.find('&', start);
  if (end == std::string_view::npos) end = body.size();
  auto action = body.substr(start, end - start);
  if (action == "start") {
    match_.start_match();
  } else if (action == "stop") {
    match_.stop_match();
  } else if (action == "skip-to-teleop") {
    match_.skip_to_teleop();
  }
}

void WebUI::handle_set_control(std::string_view body) {
  // Tiny form-encoded parser: split on '&', each piece split on '='.
  auto extract = [&](std::string_view key) -> std::string_view {
    auto needle = std::format("{}=", key);
    auto pos = body.find(needle);
    if (pos == std::string_view::npos) return {};
    if (pos != 0 && body[pos - 1] != '&') return {};
    auto start = pos + needle.size();
    auto end = body.find('&', start);
    if (end == std::string_view::npos) end = body.size();
    return body.substr(start, end - start);
  };

  auto enabled = extract("enabled");
  if (!enabled.empty()) state_.set_enabled(enabled == "true" || enabled == "1");

  auto estop = extract("estop");
  if (!estop.empty()) state_.set_e_stop(estop == "true" || estop == "1");

  auto mode = extract("mode");
  if (!mode.empty()) {
    int m = 0;
    std::from_chars(mode.data(), mode.data() + mode.size(), m);
    state_.set_mode(static_cast<RobotMode>(m >= 0 && m <= 3 ? m : 0));
  }

  auto alliance = extract("alliance");
  if (!alliance.empty()) {
    int a = 0;
    std::from_chars(alliance.data(), alliance.data() + alliance.size(), a);
    if (a < 0) a = 0;
    if (a > 3) a = 3;
    state_.set_alliance(a);
  }
}

void WebUI::start(int port) {
  server_ = std::make_unique<httplib::Server>();

  server_->Get("/", [this](const httplib::Request&, httplib::Response& res) {
    res.set_content(std::string{root_html()}, "text/html; charset=utf-8");
  });
  server_->Get("/api/state",
               [this](const httplib::Request&, httplib::Response& res) {
                 res.set_content(handle_state(), "application/json");
               });
  server_->Get("/healthz",
               [this](const httplib::Request&, httplib::Response& res) {
                 int code = 200;
                 auto body = handle_healthz(&code);
                 res.status = code;
                 res.set_content(body, "application/json");
               });
  server_->Post("/api/control",
                [this](const httplib::Request& req, httplib::Response& res) {
                  handle_set_control(req.body);
                  res.status = 204;
                });
  server_->Post("/api/match",
                [this](const httplib::Request& req, httplib::Response& res) {
                  handle_match(req.body);
                  res.status = 204;
                });

  bound_port_ = port;
  thread_ = std::thread{[this, port] { server_->listen("127.0.0.1", port); }};
}

void WebUI::stop() {
  if (server_) {
    server_->stop();
    if (thread_.joinable()) thread_.join();
    server_.reset();
  }
}

}  // namespace dssurrogate
