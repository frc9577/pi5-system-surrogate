#!/usr/bin/env bash
# Bring up a virtual CAN interface named can_s0 — matches the udev rule
# in pi-os/, so HAL CAN code finds it as bus index 0.
#
# Requires root.

set -euo pipefail

IFACE=${IFACE:-can_s0}

if ! lsmod | grep -q '^vcan'; then
  modprobe vcan
fi

if ! ip link show "$IFACE" &>/dev/null; then
  ip link add dev "$IFACE" type vcan
fi

ip link set up "$IFACE"

echo "vcan: $IFACE up" >&2
ip -details link show "$IFACE" >&2
