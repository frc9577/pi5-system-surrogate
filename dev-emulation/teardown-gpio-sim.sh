#!/usr/bin/env bash
# Tear down the gpio-sim chip created by setup-gpio-sim.sh. Idempotent.

set -euo pipefail

NAME=${NAME:-ds-surrogate}
CONFIG=/sys/kernel/config/gpio-sim/$NAME

if [[ ! -d $CONFIG ]]; then
  exit 0
fi

echo 0 > "$CONFIG/live" 2>/dev/null || true

for d in "$CONFIG"/bank0/line*; do
  [[ -d $d ]] && rmdir "$d"
done
rmdir "$CONFIG/bank0" 2>/dev/null || true
rmdir "$CONFIG" 2>/dev/null || true

echo "gpio-sim: '$NAME' torn down" >&2
