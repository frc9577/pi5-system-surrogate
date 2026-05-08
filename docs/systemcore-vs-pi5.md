# SystemCore (real hardware) vs. Pi 5 (our development analog)

## Why this matters

The team is developing 2027 robot code on a **Raspberry Pi 5 + Waveshare 2-Channel CAN FD HAT** because real Limelight SystemCore hardware doesn't ship in time for our preseason. Both targets run upstream WPILib unmodified — that's what makes the Pi a viable analog. This doc captures the deltas so it's clear at a glance which parts of our stack mirror real hardware (and survive the 2027 transition) and which are scaffolding (and get thrown away).

## Hardware

Both platforms use the same SoC family: SystemCore is a Raspberry Pi Compute Module 5 inside a Limelight-built carrier; the Pi 5 is the standalone board with the same BCM2712. The deltas are everything Limelight added around the CM5.

| | Stock Pi 5 (our setup) | Limelight SystemCore |
| --- | --- | --- |
| Main SoC | BCM2712 (Cortex-A76 ×4 @ 2.4 GHz) | BCM2712 on a CM5 — **same CPU silicon** |
| Realtime I/O coprocessor | None | **RP2350 microcontroller** for SmartIo — handles deterministic DIO/PWM/analog timing off the Linux CPU |
| Reconfigurable I/O channels | 40-pin GPIO header, all driven by the Pi SoC; no analog | 6 channels (`kNumSmartIo = 6`), each runtime-switchable to DIO / PWM out / analog in / duty-cycle in / counter / addressable-LED — all behind the RP2350 |
| CAN | None onboard. We add a Waveshare 2-Channel CAN FD HAT (2× MCP251xFD over SPI) | Built-in CAN PHYs. HAL is sized for **up to 25 buses** (`kNumCanBuses = 25`) named `can_s0..can_s24` (classic) and `can_d0..can_d24` (FD); production hardware exposes some subset of those. |
| I²C | Pi-driven on header pins | 2 dedicated I²C buses (`kNumI2cBuses = 2`) |
| IMU | None | Onboard IMU — HAL publishes `/imu/rawaccel`, `/imu/rawgyro`, `/imu/quat`, plus Euler angles and yaw in flat / landscape / portrait orientations ([IMU.cpp](../hal-port/upstream/allwpilib/hal/src/main/native/systemcore/IMU.cpp)) |
| RSL | None | Driven by Limelight's daemon based on enable state |
| Display | None | Onboard LCD |
| Status LEDs | Power/activity only | FRC-aware status LEDs |
| Power input | USB-C 5 V (5 A PD) | FRC 12 V battery, brownout/voltage-aware |
| Networking | GbE + WiFi 5 + BT5.0 | GbE; WiFi unspecified in public docs |
| Form factor | Bare board, no enclosure | Enclosed FRC-ready controller |

The Pi 5 has the same brain but **none of the FRC-specific peripherals**.

## Software stack

`systemcore-os` is a whole OS image. Our equivalent is three things stacked: stock 64-bit Pi OS, our [`pi-os/`](../pi-os/) configs, and the [`daemon/`](../daemon/) (the `pi5-system-surrogate` binary). The comparison below is between what Limelight's OS provides and what those three together deliver.

| Capability | Limelight `systemcore-os` | Our stand-in |
| --- | --- | --- |
| Base OS | Custom Limelight Linux distro (built from public sources at [LimelightVision/systemcore-os-public](https://github.com/LimelightVision/systemcore-os-public)) | Vanilla Debian (Pi OS 64-bit) |
| WPILib HAL | Bundled, target-built for SystemCore | Hand-built from upstream `linuxsystemcore` target — bit-for-bit the same source ([hal-port/](../hal-port/)) |
| `localhost:6810` NT4 server | Bundled `system-server` daemon | Our `pi5-system-surrogate` daemon (see [daemon-design.md](daemon-design.md)) |
| SmartIo `/io/{ch}/*` topics | Daemon talks to RP2350 over its private bus → real timing-deterministic I/O | Daemon talks to libgpiod on the Pi's 40-pin header → no hardware PWM timing, software best-effort |
| DS bridge | Speaks the **closed FIRST DS wire protocol** to the real DS app, translates to `mrc::ControlData` | Skips the closed wire entirely; ingests gamepad input from a laptop helper (SDL2/evdev) over NT4 + a tiny web UI for enable/disable |
| IMU `/imu/*` topics | Real onboard IMU data | Daemon publishes zeros so the HAL has something to read |
| `/sys/battery` | Real V<sub>in</sub> from FRC battery | Daemon publishes a fake constant (12.0 V) |
| CAN bring-up | `can_s0..` come up automatically | `can0` renamed → `can_s0` via udev, brought up by systemd-networkd at 1 Mbps classic-CAN |
| RSL | Driven by daemon → physical RSL output | Daemon drives a libgpiod GPIO pin (we wire an LED to it) |
| Robot service | systemd unit deploys + restarts robot.jar; tightly integrated with hardware safety | Equivalent systemd unit; no hardware safety — disable just stops outputs in software |
| FMS / network config | Team-number-aware, FRC-standard `10.TE.AM.2` | Configured to match (see [pi-os/](../pi-os/)) |
| OTA updates | Limelight Hardware Manager | None — manual updates |
| Vendor JNI support | Officially supported `linuxsystemcore` target for Phoenix 6 / REVLib | Same `linuxsystemcore` artifacts; should link, but officially unsupported on non-Limelight hardware |
| Production lockdown | Locked-down, tested, FRC-rules-compliant | Dev scaffolding — hackable, not for competition |

## What carries forward to real SystemCore

These are the things the team's effort builds equity in. They keep working unchanged when SystemCore arrives:

- Robot code in [`robot-java/`](../robot-java/) — written against WPILib 2027, runs on real SystemCore as-is
- Vendor library usage (Phoenix 6, REVLib) — same APIs, same JNI variant
- Understanding of the 2027 control-system architecture (NT-on-localhost system-server, SmartIo channels, DS protobuf topics)
- Network conventions (`10.TE.AM.2`, mDNS) — already match real FRC

## What we throw away when SystemCore arrives

These are scaffolding. They should not accumulate features beyond what the team needs to make progress today:

- `daemon/` (`pi5-system-surrogate` binary) — replaced by Limelight's `system-server`
- `pi-os/` configs (udev, systemd-networkd) — SystemCore image owns this
- `hal-port/` custom HAL build — SystemCore ships its own
- Pi-specific GPIO pin maps — SmartIo channels are physical on real hardware

## Implications for design choices

- **Don't fork the HAL.** The whole architectural premise is that the upstream `linuxsystemcore` HAL builds and runs unmodified. Anything we'd want to "fix" in the HAL is really a bug in our stand-in daemon's NT contract.
- **Keep the daemon thin.** It is throwaway. Optimize for "easy to read, easy to modify" over performance or polish — see [daemon-design.md](daemon-design.md).
- **Don't simulate hardware we don't have.** No fake IMU motion, no fake match timer, no fake brownouts. Publish zeros / constants and let the robot code read them. Adding fidelity to a stand-in is wasted effort once real hardware arrives.
- **Match real FRC conventions where it's free.** Network IPs, hostnames, CAN interface names, NT topic paths — keeping these aligned means less to change when migrating, and the team builds correct muscle memory.
