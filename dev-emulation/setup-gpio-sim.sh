#!/usr/bin/env bash
# Set up a virtual GPIO chip with 28 lines (covers Pi-style GPIO 0..27)
# via the kernel's gpio-sim driver. Lets us run the libgpiod backend
# without real hardware. Prints the resulting /dev/gpiochipN path on
# stdout; everything else on stderr.
#
# Requires root (configfs writes).

set -euo pipefail

NAME=${NAME:-pi5-system-surrogate}
NUM_LINES=${NUM_LINES:-28}
CONFIG=/sys/kernel/config/gpio-sim/$NAME

if ! mountpoint -q /sys/kernel/config; then
  mount -t configfs configfs /sys/kernel/config 2>/dev/null || true
fi

if ! lsmod | grep -q '^gpio_sim'; then
  modprobe gpio-sim
fi

if [[ -d $CONFIG ]]; then
  echo "gpio-sim '$NAME' already exists; tearing down first" >&2
  echo 0 > "$CONFIG"/live 2>/dev/null || true
  for d in "$CONFIG"/bank0/line*; do [[ -d $d ]] && rmdir "$d"; done
  rmdir "$CONFIG"/bank0 2>/dev/null || true
  rmdir "$CONFIG" 2>/dev/null || true
fi

mkdir -p "$CONFIG/bank0"
echo "$NUM_LINES" > "$CONFIG/bank0/num_lines"

# Pre-create line dirs so debugfs paths are predictable as line<N>.
for ((i = 0; i < NUM_LINES; i++)); do
  mkdir -p "$CONFIG/bank0/line$i"
  echo "gpio$i" > "$CONFIG/bank0/line$i/name"
done

echo 1 > "$CONFIG/live"

CHIP=$(< "$CONFIG/bank0/chip_name")
DEV_NAME=$(< "$CONFIG/dev_name")
echo "gpio-sim: dev=$DEV_NAME chip=$CHIP num_lines=$NUM_LINES" >&2
echo "gpio-sim: debugfs root: /sys/kernel/debug/gpio-sim/$DEV_NAME/bank0/" >&2

# Stdout: just the device path, for shell capture.
echo "/dev/$CHIP"
