# dev-emulation

Validate `ds-surrogate` end-to-end without a real Pi by standing up
**virtual** kernel devices that present the same userspace API as
hardware:

- **`gpio-sim`** — virtual GPIO chip; `libgpiod` can't tell it apart
  from a real `/dev/gpiochip0`. `setup-gpio-sim.sh` creates a chip with
  28 lines (covers Pi-style GPIO 0..27), exposes a debugfs interface so
  the integration test can read what the daemon drove.
- **`vcan`** — virtual SocketCAN; the WPILib HAL's CAN code (`CAN.cpp`)
  speaks pure SocketCAN and works transparently against `vcan0`. Our
  daemon doesn't touch CAN itself, but a `robot.jar` running against the
  daemon would, and `candump can_s0` lets you sniff frames live.

**What this validates:** the libgpiod backend, the SmartIo NT bridge end
to end, the RSL blinker, the web UI, and the daemon's `HAL_Initialize`
contract. **What it doesn't:** real GPIO drive strength, real
MCP251xFD/SPI timing, real motors. Those still need a Pi on the bench.

## Prerequisites

- Linux kernel with `gpio-sim` and `vcan` modules (mainline ≥ 5.18 for
  gpio-sim; vcan has been there forever).
- `ds-surrogate` built at `../ds-surrogate/build/`.
- `curl` and `jq` (`jq` is optional but the test uses `grep` as a
  fallback).
- `root` for configfs writes and `ip link add`.

## Quick start

```bash
sudo ./run-integration-test.sh
```

Sample passing output:

```
=== bring up virtual hardware ===
gpio-sim: dev=gpio-sim.0 chip=gpiochip5 num_lines=28
vcan: can_s0 up
=== start daemon ===
=== Phase 1: HAL contract (ServerReady + ControlData) ===
ServerReady=true (after 20 ms)
ControlData received (2 bytes, DsConnected=true) after 80 ms
=== Phase 2: SmartIo bridge — drive ch0 (GPIO17) high ===
[PASS] gpio-sim line gpio17 reads 1 (high)
[PASS] gpio-sim line gpio17 reads 0 (low)
=== Phase 3: RSL blink when enabled (GPIO26) ===
[PASS] RSL toggled enabled-state: 0 → 1
=== Phase 4: /healthz returns 200/ok ===
[PASS] /healthz: {"status":"ok","cadence_p99_ms":20.41,"uptime_s":2.21}

=== ALL PASS — daemon validated end-to-end against virtual hardware ===
```

## Manual use

If you want to play with the daemon under emulation interactively:

```bash
sudo ./setup-gpio-sim.sh    # prints the chip path on stdout
sudo ./setup-vcan.sh
DS_SURROGATE_GPIOCHIP=/dev/gpiochip5 ../ds-surrogate/build/ds-surrogate &

# inspect what the daemon drove:
cat /sys/kernel/debug/gpio-sim/gpio-sim.0/bank0/gpio17/value

# pretend an input is high (drives the daemon's input read):
echo pull-up > /sys/kernel/debug/gpio-sim/gpio-sim.0/bank0/gpio22/pull

# sniff CAN traffic from a robot.jar:
candump can_s0

# tear down:
sudo ./teardown-vcan.sh
sudo ./teardown-gpio-sim.sh
```

## Daemon env-var override

`ds-surrogate`'s libgpiod backend defaults to `/dev/gpiochip0`. The
gpio-sim chip allocates the next available number (often `gpiochip5`+ on
hosts that already have onboard GPIO). Set:

```
DS_SURROGATE_GPIOCHIP=/dev/gpiochip5
```

…to point at the simulated chip. The integration test does this
automatically.

## Reading what the daemon drove

`gpio-sim` exposes per-line state under
`/sys/kernel/debug/gpio-sim/<dev>/bank0/<line>/`:

- `value` (RO) — current line value, regardless of direction
- `pull` (RW) — for input lines, write `pull-up` or `pull-down` to drive
  what `gpiod_line_get_value` returns

For our daemon, the relevant lines are:

| GPIO | SmartIo channel | Use |
| --- | --- | --- |
| 17 | 0 | DIO |
| 22 | 2 | DIO |
| 23 | 3 | DIO |
| 27 | 1 | DIO |
| 26 | (RSL only — no SmartIo channel) | RSL output |

## Why not QEMU?

QEMU has no Pi 5 (BCM2712) machine and no MCP251xFD model. Spinning up
a Pi 4 image in QEMU would test a different SoC and wouldn't help the
high-risk path (the NT4 contract). `gpio-sim` + `vcan` cover the same
contract surface much faster — the kernel API is identical, the only
thing missing is signal integrity, which an emulator wouldn't catch
either.
