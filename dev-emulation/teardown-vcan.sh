#!/usr/bin/env bash
# Tear down the vcan interface from setup-vcan.sh. Idempotent.

set -euo pipefail

IFACE=${IFACE:-can_s0}

if ip link show "$IFACE" &>/dev/null; then
  ip link set down "$IFACE" 2>/dev/null || true
  ip link delete "$IFACE" type vcan 2>/dev/null || true
  echo "vcan: $IFACE torn down" >&2
fi
