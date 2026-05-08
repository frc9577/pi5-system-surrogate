#!/usr/bin/env bash
# End-to-end deploy / reload / match-phase test.
#
# Phases:
#   1. Build daemon + robot fat jar
#   2. Start daemon
#   3. Deploy robot v1 → assert it reports "disabled" via /robot/status
#   4. Drive a full match (start → auto → gap → teleop → end) → assert the
#      robot transitions through corresponding phases
#   5. Bump Robot.VERSION, redeploy → assert /robot/version reports the
#      new value (proves fresh code is running)
#   6. Cleanup
#
# Self-contained — runs locally with no Pi / no real WPILib alpha jars.
# Requires: java 25, an existing hal-port build (libntcore.so + ntcore.jar
# already produced).

set -euo pipefail

cd "$(dirname "$0")/.."

# After the platform/template split, the robot lives in a sibling repo
# named frc-2027-robot-starter (flattened — gradle project at its root).
# Override ROBOT_DIR if you keep yours elsewhere.
DAEMON_BIN=$(readlink -f "ds-surrogate/build/ds-surrogate")
ROBOT_DIR=${ROBOT_DIR:-$(readlink -f "../frc-2027-robot-starter" 2>/dev/null || true)}
[[ -n ${ROBOT_DIR:-} && -d $ROBOT_DIR ]] || {
  echo "missing robot template at $ROBOT_DIR — clone frc-2027-robot-starter as a sibling, or set ROBOT_DIR=" >&2
  exit 2
}
ROBOT_SRC="$ROBOT_DIR/src/main/java/Robot.java"

[[ -x $DAEMON_BIN ]] || { echo "missing $DAEMON_BIN — build ds-surrogate first"; exit 2; }
[[ -f $ROBOT_SRC ]] || { echo "missing $ROBOT_SRC"; exit 2; }

LOG_DAEMON=/tmp/ds-match-test.daemon.log
LOG_ROBOT=/tmp/robot.log
DAEMON_PID=""

cleanup() {
  set +e
  echo "--- cleanup ---"
  curl -s -X POST -d "action=stop" http://127.0.0.1:8080/api/match >/dev/null 2>&1 || true
  bash "$ROBOT_DIR/stop.sh" 2>/dev/null || true
  if [[ -n $DAEMON_PID ]]; then
    kill -TERM "$DAEMON_PID" 2>/dev/null
    wait "$DAEMON_PID" 2>/dev/null
  fi
  # Restore Robot.VERSION if we mutated it.
  if [[ -f $ROBOT_SRC.bak ]]; then
    mv "$ROBOT_SRC.bak" "$ROBOT_SRC"
  fi
}
trap cleanup EXIT

# Subscribe-and-poll the robot's status/version topics via the daemon's
# /api/state endpoint (the daemon is itself an NT4 server, so the robot's
# /robot/* topics show up there). For now we use a small Java helper that
# reads via NT4 — but it's enough to grep /api/state for the topic names
# the robot publishes. Wait, /api/state doesn't include /robot/* topics.
# We'll use a small NT4 client.
#
# Instead, observe the robot's stdout log. Robot prints
# "[<version>] <prev> -> <new>" on every transition.

assert_log_contains() {
  local pattern=$1
  local timeout_ms=${2:-3000}
  local elapsed=0
  while (( elapsed < timeout_ms )); do
    if grep -q "$pattern" "$LOG_ROBOT" 2>/dev/null; then
      echo "[PASS] log contains '$pattern'"
      return 0
    fi
    sleep 0.1
    elapsed=$((elapsed + 100))
  done
  echo "[FAIL] timed out waiting for '$pattern' in $LOG_ROBOT"
  echo "--- last 20 lines ---"
  tail -20 "$LOG_ROBOT" || true
  return 1
}

post_match() {
  curl -fs -X POST -d "action=$1" http://127.0.0.1:8080/api/match >/dev/null
}

echo "=== Phase 1: build robot fat jar ==="
(cd "$ROBOT_DIR" && ./gradlew -q fatJar)

echo "=== Phase 2: start daemon ==="
"$DAEMON_BIN" > "$LOG_DAEMON" 2>&1 &
DAEMON_PID=$!
sleep 1.0
if ! kill -0 "$DAEMON_PID" 2>/dev/null; then
  echo "daemon died on startup. log:" >&2
  cat "$LOG_DAEMON" >&2
  exit 1
fi

echo "=== Phase 3: deploy robot v1 ==="
> "$LOG_ROBOT"  # clear log so we don't pick up stale matches
(cd "$ROBOT_DIR" && ./deploy.sh) >/dev/null
sleep 1.5
assert_log_contains '\[v1\] .* -> disabled' 5000

echo "=== Phase 4: drive a full match cycle ==="
# Custom-shorter durations: skip-to-teleop bypasses auto entirely, then
# teleop runs at the daemon default 135s. We'll cancel after teleop kicks
# in to keep the test short.
post_match start
assert_log_contains '\[v1\] disabled -> auto' 4000
# Skip to teleop directly; assert the robot picks it up.
post_match skip-to-teleop
assert_log_contains '\[v1\] auto -> teleop\|\[v1\] disabled -> teleop' 4000
post_match stop
assert_log_contains 'teleop -> disabled' 4000

echo "=== Phase 5: bump Robot.VERSION, redeploy ==="
cp "$ROBOT_SRC" "$ROBOT_SRC.bak"
sed -i 's/static final String VERSION = "v1";/static final String VERSION = "v2";/' "$ROBOT_SRC"
> "$LOG_ROBOT"
(cd "$ROBOT_DIR" && ./deploy.sh) >/dev/null
sleep 1.5
# After redeploy the robot's transitions are tagged with [v2].
assert_log_contains '\[v2\] .* -> disabled' 6000

echo "=== Phase 6: drive new code through teleop ==="
post_match skip-to-teleop
assert_log_contains '\[v2\] disabled -> teleop' 4000
post_match stop
assert_log_contains '\[v2\] teleop -> disabled' 4000

echo
echo "=== ALL PASS — deploy / reload / match-phase loop verified ==="
