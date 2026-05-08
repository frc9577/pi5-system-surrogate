#!/usr/bin/env bash
# End-to-end integration test for ds-surrogate, running against virtual
# kernel devices (gpio-sim + vcan) instead of real hardware.
#
# Requires:
#   - root (configfs + ip link)
#   - ds-surrogate built at ../ds-surrogate/build/
#   - curl + jq
#
# Phases:
#   1. ServerReady + ControlData contract  (check_server_ready)
#   2. SmartIo bridge                       (nt_smartio_test → gpio-sim)
#   3. RSL blink                            (web UI enable → gpio-sim line26)
#   4. /healthz HTTP                        (curl)

set -euo pipefail

cd "$(dirname "$0")"
DAEMON_BIN=$(readlink -f "../ds-surrogate/build/ds-surrogate")
CHECK_BIN=$(readlink -f "../ds-surrogate/build/check_server_ready")
NT_TOOL=$(readlink -f "../ds-surrogate/build/nt_smartio_test")

[[ -x $DAEMON_BIN ]] || { echo "missing $DAEMON_BIN — build ds-surrogate first"; exit 2; }
[[ -x $CHECK_BIN ]]  || { echo "missing $CHECK_BIN";  exit 2; }
[[ -x $NT_TOOL ]]    || { echo "missing $NT_TOOL";    exit 2; }

if [[ $EUID -ne 0 ]]; then
  echo "this script needs root (gpio-sim + vcan). re-run with sudo." >&2
  exit 1
fi

LOG=/tmp/ds-surrogate-integration.log
DAEMON_PID=""
GPIOCHIP=""
DEV_NAME=""

cleanup() {
  set +e
  if [[ -n $DAEMON_PID ]]; then
    kill -TERM "$DAEMON_PID" 2>/dev/null
    wait "$DAEMON_PID" 2>/dev/null
  fi
  ./teardown-vcan.sh
  ./teardown-gpio-sim.sh
}
trap cleanup EXIT

echo "=== bring up virtual hardware ==="
GPIOCHIP=$(./setup-gpio-sim.sh)
./setup-vcan.sh
DEV_NAME=$(< /sys/kernel/config/gpio-sim/ds-surrogate/dev_name)
DEBUGFS=/sys/kernel/debug/gpio-sim/$DEV_NAME/bank0
echo "gpio-sim chip:  $GPIOCHIP"
echo "gpio-sim debugfs: $DEBUGFS"

echo "=== start daemon ==="
DS_SURROGATE_GPIOCHIP=$GPIOCHIP "$DAEMON_BIN" > "$LOG" 2>&1 &
DAEMON_PID=$!
sleep 1.0

if ! kill -0 "$DAEMON_PID" 2>/dev/null; then
  echo "daemon died during startup. log:" >&2
  cat "$LOG" >&2
  exit 1
fi

echo "=== Phase 1: HAL contract (ServerReady + ControlData) ==="
"$CHECK_BIN"

echo "=== Phase 2: SmartIo bridge — drive ch0 (GPIO17) high ==="
"$NT_TOOL" 0 1 255   # ch=0, type=1 (DIO output), valset=255 (high)
sleep 0.2
val=$(< "$DEBUGFS/gpio17/value")
if [[ $val == "1" ]]; then
  echo "[PASS] gpio-sim line gpio17 reads $val (high)"
else
  echo "[FAIL] gpio-sim line gpio17 reads $val (expected 1)"
  exit 1
fi

"$NT_TOOL" 0 1 0     # drive low
sleep 0.2
val=$(< "$DEBUGFS/gpio17/value")
if [[ $val == "0" ]]; then
  echo "[PASS] gpio-sim line gpio17 reads $val (low)"
else
  echo "[FAIL] gpio-sim line gpio17 reads $val (expected 0)"
  exit 1
fi

echo "=== Phase 3: RSL blink when enabled (GPIO26) ==="
curl -fs -X POST -d "enabled=true" http://127.0.0.1:8080/api/control >/dev/null
sleep 0.6
v1=$(< "$DEBUGFS/gpio26/value")
sleep 0.6
v2=$(< "$DEBUGFS/gpio26/value")
if [[ $v1 != "$v2" ]]; then
  echo "[PASS] RSL toggled enabled-state: $v1 → $v2"
else
  echo "[FAIL] RSL did not toggle (saw $v1 twice)"
  exit 1
fi

curl -fs -X POST -d "enabled=false" http://127.0.0.1:8080/api/control >/dev/null

echo "=== Phase 4: /healthz returns 200/ok ==="
status=$(curl -s -o /tmp/healthz.json -w '%{http_code}' http://127.0.0.1:8080/healthz)
if [[ $status == "200" ]] && grep -q '"status":"ok"' /tmp/healthz.json; then
  echo "[PASS] /healthz: $(< /tmp/healthz.json)"
else
  echo "[FAIL] /healthz: HTTP $status, body=$(< /tmp/healthz.json)"
  exit 1
fi

echo
echo "=== ALL PASS — daemon validated end-to-end against virtual hardware ==="
