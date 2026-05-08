# Architecture

## Goal

Build a development analog of the FRC 2027 SystemCore control system on a
Raspberry Pi 5 + Waveshare 2-Channel CAN FD HAT, using currently-available
official FRC sources, so the team can develop portable robot code and learn
the new architecture before SystemCore hardware ships.

Scope is intentionally narrow: CAN devices, RSL, and 4 digital I/O.

## Companion docs

- [systemcore-vs-pi5.md](systemcore-vs-pi5.md) — what's different between real SystemCore and our Pi 5 analog, and which parts of our stack survive the 2027 transition vs. get thrown away
- [daemon-design.md](daemon-design.md) — design of the `ds-surrogate` daemon: language, modules, full NT4 topic contract, GPIO pin map, startup/failure semantics

## Reference: how real SystemCore is built

SystemCore is a Limelight-built controller running a Raspberry Pi Compute
Module 5 alongside an RP2350 microcontroller that handles "smart" I/O (DIO,
PWM, analog). It runs Linux on the CM5. The WPILib 2027 HAL has a dedicated
`hal/src/main/native/systemcore/` directory with the implementation that
ships on real hardware.

Reading the upstream SystemCore HAL, the key pieces are all
**NetworkTables-based**, not hardware-coupled:

| Subsystem | SystemCore implementation | Pi 5 portability |
| --- | --- | --- |
| CAN | Pure SocketCAN (`PF_CAN`/`SOCK_RAW`), interfaces named `can_s0..can_s4` (classic) and `can_d0..` (FD) | Identical — bring `can0` up as `can_s0` via udev + systemd-networkd |
| Digital I/O / PWM / Analog | `SmartIo` is an NT4 client that publishes `/io/{ch}/{type,valset,valget,periodset,…}` to a system-server daemon at `localhost:6810`. The system-server daemon is what talks to the RP2350. | HAL unchanged; we write a Pi-side system-server daemon that subscribes to the same topics and bridges to libgpiod |
| Robot Signal Light | No dedicated HAL file; driven by DS enable state through the system server | Our system server toggles a libgpiod line at ~1 Hz when enabled, solid when disabled |
| Driver Station | NT4 protobuf pub/sub: subscribes to `mrc::ControlData`, `mrc::MatchInfo`, joystick descriptors, joystick outputs. A separate system-server brokers between the closed DS↔server wire and these NT topics | Robot side is unchanged. Our DS surrogate publishes the same NT topics |

**Key insight:** the entire SystemCore HAL is NT4-on-localhost. The *only*
thing it talks to outside that NT bus is SocketCAN. The "system server" is
the catch-all process on the SystemCore image that owns the actual hardware
(RP2350, RSL line, USB joysticks via DS, etc.) and exposes it via NT topics.
On Pi 5 we write a substitute system-server that owns libgpiod (and forwards
DS state from a laptop) — the WPILib HAL itself is bit-for-bit identical to
upstream.

## Our stack on Pi 5

```
                    closed wire protocol         NT4 protobuf
[FIRST Driver Station]  ────X────► [DS Surrogate (laptop)]  ──────┐
       (stretch goal only)                                         │
                                                                   ▼
                                                        ┌─────────────────────┐
                                                        │  Robot Code (Java)  │
                                                        │  WPILib 2027 alpha  │
                                                        │  TimedRobot         │
                                                        └──────────┬──────────┘
                                                                   │
                                                          ┌────────┴────────┐
                                                          ▼                 ▼
                                                 ┌─────────────────┐  ┌────────────┐
                                                 │ Custom HAL      │  │ Vendor JNI │
                                                 │ (linux-arm64)   │  │ Phoenix 6  │
                                                 └────────┬────────┘  │ REVLib     │
                                                          │           └────────────┘
                                          ┌───────────────┼────────────────────┐
                                          ▼               ▼                    ▼
                                    ┌──────────┐   ┌─────────────┐   ┌─────────────────┐
                                    │ SocketCAN│   │ libgpiod v2 │   │ libgpiod v2     │
                                    │ can_s0   │   │ (4 DIO)     │   │ (RSL @ 1 Hz)    │
                                    │ MCP251xFD│   │ /dev/gpiochipN  │ /dev/gpiochipN  │
                                    └──────────┘   └─────────────┘   └─────────────────┘
```

## Component plan

### Pi OS layer (`pi-os/`)

- 64-bit Raspberry Pi OS (Debian-based — same family as SystemCore's image)
- `mcp251xfd` kernel driver loaded for the Waveshare CAN FD HAT
- udev rule renames `can0` → `can_s0` so the HAL CAN code finds it
- systemd-networkd brings the interface up at FRC's standard 1 Mbps
  classic-CAN bitrate
- libgpiod v2 in userspace for DIO and the RSL line
- FRC-standard `10.TE.AM.2` networking on the onboard Ethernet, derived at
  boot from a single integer file on the boot partition
  (`/boot/firmware/frc-team-number`). Hostname follows the roboRIO pattern
  as `systemcore-{team}-frc`. See [pi-os/README.md](../pi-os/README.md).

### HAL build (`hal-port/`)

The upstream SystemCore HAL builds **unchanged** for the `linuxsystemcore`
platform on aarch64 hosts. One config tweak required: bump the opensdk
toolchain tag from `v2025-1` to `v2025-2` (the v2025-1 release is missing
the aarch64-host SystemCore toolchain bundle).

Build command (after JDK 25 + libgpiod-dev installed):

```
cd hal-port/upstream/allwpilib
./gradlew installSystemCoreToolchain -Ponlylinuxsystemcore
./gradlew :hal:halJNISharedReleaseSharedLibrary -Ponlylinuxsystemcore
```

Produces `libwpiHal.so`, `libwpiHaljni.so`, and the supporting NT/wpiutil/
wpinet/datalog `.so`s — all aarch64 ELF, ready to deploy to the Pi.

Vendor libraries (Phoenix 6 `25.90.0-alpha-2` and REVLib `2027.0.0-alpha-1`)
both ship `linuxsystemcore` JNI artifacts that link against this HAL.

See [hal-port/README.md](../hal-port/README.md) for full build details.

### System server / DS surrogate (`ds-surrogate/`)

Stand-in for Limelight's `system-server` daemon: NT4 server on
`localhost:6810` that bridges SmartIo topics to libgpiod and synthesizes
`mrc::ControlData` from a laptop-side gamepad helper plus a tiny web UI.
Full design — language choice, modules, NT4 topic contract, GPIO pin map,
startup sequence, failure modes — is in
[daemon-design.md](daemon-design.md).

The Limelight `system-server` binary is distributed inside the SystemCore
OS image but is hardware-bound (drives the RP2350; speaks the closed FIRST
DS wire protocol on its inbound side), so running the real binary on a Pi
5 with no RP2350 isn't viable. Our stand-in is intentionally throwaway —
when SystemCore arrives, the robot code keeps running unchanged and this
daemon retires.

### Robot code (`robot-java/`)

Standard WPILib 2027 alpha Java project, generated by the WPILib project
generator once the HAL port is at least Hello-World running. Demo content
will exercise:

- One CTRE Talon FX over Phoenix 6
- One REV SparkMax over REVLib
- Four DIO lines (read inputs, drive outputs)
- The RSL (driven implicitly by DS enable state via the HAL)

## Open questions

- **Vendor lib compatibility on linux-arm64.** Phoenix 6 and REVLib both
  ship arm64 natives, but it's not yet confirmed they'll cleanly link
  against a hand-built systemcore HAL outside Limelight's image.
- **CAN FD on `can_s0`.** SystemCore distinguishes classic (`can_s*`) from
  FD (`can_d*`) interfaces. Our HAT supports FD, but FRC vendor devices are
  classic-only at 1 Mbps for now, so we configure it as `can_s0`.
- **Real DS app integration.** Stretch — revisit when WPILib publishes the
  system-server protocol or releases a portable build.

## References

- WPILib all-in-one repo: https://github.com/wpilibsuite/allwpilib
- SystemCore HAL source: https://github.com/wpilibsuite/allwpilib/tree/main/hal/src/main/native/systemcore
- SystemCore HAL CAN: https://github.com/wpilibsuite/allwpilib/blob/main/hal/src/main/native/systemcore/CAN.cpp
- SystemCore HAL DIO: https://github.com/wpilibsuite/allwpilib/blob/main/hal/src/main/native/systemcore/DIO.cpp
- SystemCore HAL DriverStation: https://github.com/wpilibsuite/allwpilib/blob/main/hal/src/main/native/systemcore/FIRSTDriverStation.cpp
- SystemCore alpha/beta testing repo: https://github.com/wpilibsuite/SystemcoreTesting
- 2027 FIRST Driver Station (closed source, public docs/issues): https://github.com/wpilibsuite/FirstDriverStation-Public
- 2027 WPILib docs (SystemCore section): https://docs.wpilib.org/en/2027/docs/software/systemcore-info/index.html
- 2027 Driver Station blog post: https://wpilib.org/blog/the-2027-first-driver-station
- 2026 WPILib kickoff release notes: https://wpilib.org/blog/2026-kickoff-release-of-wpilib
