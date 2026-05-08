#include "web_ui.hpp"

#include "cadence_tracker.hpp"
#include "daemon_state.hpp"
#include "match_controller.hpp"
#include "nt_server.hpp"
#include "smartio_bridge.hpp"
#include "smartio_state.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string>

using dssurrogate::CadenceTracker;
using dssurrogate::DaemonState;
using dssurrogate::IGpioBackend;
using dssurrogate::MatchController;
using dssurrogate::MatchPhase;
using dssurrogate::RobotMode;
using dssurrogate::SmartIoBridge;
using dssurrogate::WebUI;
using namespace std::chrono_literals;

namespace {

class StubBackend final : public IGpioBackend {
 public:
  void release_line(int) override {}
  void request_input(int) override {}
  void request_output(int, bool) override {}
  void set_value(int, bool) override {}
  bool get_value(int) override { return false; }
};

constexpr std::array<SmartIoBridge::PinMapping, 2> kMap{{{0, 17}, {1, 27}}};

struct WebUIFixture {
  dssurrogate::NtServer server{56840};
  StubBackend backend;
  DaemonState state;
  CadenceTracker cadence;
  SmartIoBridge bridge{server.instance(), backend, kMap};
  // Short match durations so the phase tests can fire in milliseconds.
  MatchController match{state, MatchController::Durations{
                                   .auto_period = 200ms,
                                   .auto_teleop_gap = 100ms,
                                   .teleop_period = 200ms,
                               }};
  WebUI ui{state, cadence, bridge, match};
};

}  // namespace

TEST(WebUI, RootHtmlContainsRefreshScript) {
  auto html = std::string{WebUI::root_html()};
  EXPECT_NE(html.find("setInterval"), std::string::npos);
  EXPECT_NE(html.find("/api/state"), std::string::npos);
}

TEST(WebUI, StateJsonReflectsDaemonState) {
  WebUIFixture f;
  f.state.set_enabled(true);
  f.state.set_mode(RobotMode::Teleop);
  f.state.set_alliance(2);

  auto json = f.ui.handle_state();
  EXPECT_NE(json.find(R"("enabled":true)"), std::string::npos);
  EXPECT_NE(json.find(R"("mode":"teleop")"), std::string::npos);
  EXPECT_NE(json.find(R"("alliance":2)"), std::string::npos);
}

TEST(WebUI, HealthzGreenWhenCadenceLow) {
  WebUIFixture f;
  for (int i = 0; i < 100; ++i) f.cadence.record_interval_us(5000);
  int code = 0;
  auto body = f.ui.handle_healthz(&code);
  EXPECT_EQ(code, 200);
  EXPECT_NE(body.find(R"("status":"ok")"), std::string::npos);
}

TEST(WebUI, HealthzRedWhenCadenceBad) {
  WebUIFixture f;
  for (int i = 0; i < 100; ++i) f.cadence.record_interval_us(50'000);
  int code = 0;
  auto body = f.ui.handle_healthz(&code);
  EXPECT_EQ(code, 503);
  EXPECT_NE(body.find(R"("status":"degraded")"), std::string::npos);
}

TEST(WebUI, SetControlEnabledTrue) {
  WebUIFixture f;
  f.ui.handle_set_control("enabled=true");
  EXPECT_TRUE(f.state.snapshot_control().enabled);
  f.ui.handle_set_control("enabled=false");
  EXPECT_FALSE(f.state.snapshot_control().enabled);
}

TEST(WebUI, SetControlMode) {
  WebUIFixture f;
  f.ui.handle_set_control("mode=2");
  EXPECT_EQ(f.state.snapshot_control().mode, RobotMode::Teleop);
  f.ui.handle_set_control("mode=3");
  EXPECT_EQ(f.state.snapshot_control().mode, RobotMode::Test);
}

TEST(WebUI, SetControlAllianceClampedToValidRange) {
  WebUIFixture f;
  f.ui.handle_set_control("alliance=99");
  EXPECT_EQ(f.state.snapshot_control().alliance, 3);
  f.ui.handle_set_control("alliance=-7");
  EXPECT_EQ(f.state.snapshot_control().alliance, 0);
  f.ui.handle_set_control("alliance=2");
  EXPECT_EQ(f.state.snapshot_control().alliance, 2);
}

TEST(WebUI, SetControlMultipleFieldsInOneRequest) {
  WebUIFixture f;
  f.ui.handle_set_control("enabled=true&mode=1&alliance=1&estop=false");
  auto v = f.state.snapshot_control();
  EXPECT_TRUE(v.enabled);
  EXPECT_EQ(v.mode, RobotMode::Auto);
  EXPECT_EQ(v.alliance, 1);
  EXPECT_FALSE(v.e_stop);
}

TEST(WebUI, SetControlIgnoresUnknownKeys) {
  WebUIFixture f;
  f.ui.handle_set_control("foo=bar&baz=quux");
  auto v = f.state.snapshot_control();
  EXPECT_FALSE(v.enabled);
  EXPECT_EQ(v.mode, RobotMode::Unknown);
}

TEST(WebUI, StateJsonIncludesMatchPhase) {
  WebUIFixture f;
  EXPECT_NE(f.ui.handle_state().find(R"("phase":"idle")"), std::string::npos);
  f.ui.handle_match("action=start");
  EXPECT_NE(f.ui.handle_state().find(R"("phase":"auto")"), std::string::npos);
}

TEST(WebUI, MatchActionStartTransitionsToAuto) {
  WebUIFixture f;
  f.ui.handle_match("action=start");
  EXPECT_EQ(f.match.snapshot().phase, MatchPhase::Auto);
  EXPECT_TRUE(f.state.snapshot_control().enabled);
  EXPECT_EQ(f.state.snapshot_control().mode, RobotMode::Auto);
}

TEST(WebUI, MatchActionStopReturnsToIdle) {
  WebUIFixture f;
  f.ui.handle_match("action=start");
  f.ui.handle_match("action=stop");
  EXPECT_EQ(f.match.snapshot().phase, MatchPhase::Idle);
  EXPECT_FALSE(f.state.snapshot_control().enabled);
}

TEST(WebUI, MatchActionSkipToTeleop) {
  WebUIFixture f;
  f.ui.handle_match("action=skip-to-teleop");
  EXPECT_EQ(f.match.snapshot().phase, MatchPhase::Teleop);
  EXPECT_EQ(f.state.snapshot_control().mode, RobotMode::Teleop);
}

TEST(WebUI, MatchActionUnknownIsNoOp) {
  WebUIFixture f;
  f.ui.handle_match("action=launch-rocket");
  EXPECT_EQ(f.match.snapshot().phase, MatchPhase::Idle);
}

// HTTP integration tests — exercise the listener thread.

// httplib included after the unit-test block so its 8000+ lines don't
// expand into the rest of the file's compilation more than once.
#include "httplib.h"

namespace {
constexpr int kHttpTestPort = 18080;
}  // namespace

TEST(WebUIHttp, RootReturnsHtml) {
  WebUIFixture f;
  f.ui.start(kHttpTestPort);
  std::this_thread::sleep_for(50ms);

  httplib::Client cli{"127.0.0.1", kHttpTestPort};
  auto res = cli.Get("/");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);
  EXPECT_NE(res->body.find("setInterval"), std::string::npos);

  f.ui.stop();
}

TEST(WebUIHttp, ApiStateReturnsJson) {
  WebUIFixture f;
  f.state.set_enabled(true);
  f.ui.start(kHttpTestPort + 1);
  std::this_thread::sleep_for(50ms);

  httplib::Client cli{"127.0.0.1", kHttpTestPort + 1};
  auto res = cli.Get("/api/state");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);
  EXPECT_EQ(res->get_header_value("Content-Type"), "application/json");
  EXPECT_NE(res->body.find(R"("enabled":true)"), std::string::npos);

  f.ui.stop();
}

TEST(WebUIHttp, HealthzReturns200WhenHealthy) {
  WebUIFixture f;
  for (int i = 0; i < 100; ++i) f.cadence.record_interval_us(5000);
  f.ui.start(kHttpTestPort + 2);
  std::this_thread::sleep_for(50ms);

  httplib::Client cli{"127.0.0.1", kHttpTestPort + 2};
  auto res = cli.Get("/healthz");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);

  f.ui.stop();
}

TEST(WebUIHttp, HealthzReturns503WhenDegraded) {
  WebUIFixture f;
  for (int i = 0; i < 100; ++i) f.cadence.record_interval_us(50'000);
  f.ui.start(kHttpTestPort + 3);
  std::this_thread::sleep_for(50ms);

  httplib::Client cli{"127.0.0.1", kHttpTestPort + 3};
  auto res = cli.Get("/healthz");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 503);

  f.ui.stop();
}

TEST(WebUIHttp, PostControlReturns204AndUpdatesState) {
  WebUIFixture f;
  f.ui.start(kHttpTestPort + 4);
  std::this_thread::sleep_for(50ms);

  httplib::Client cli{"127.0.0.1", kHttpTestPort + 4};
  auto res = cli.Post("/api/control", "enabled=true&mode=2",
                      "application/x-www-form-urlencoded");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 204);

  auto v = f.state.snapshot_control();
  EXPECT_TRUE(v.enabled);
  EXPECT_EQ(v.mode, RobotMode::Teleop);

  f.ui.stop();
}
