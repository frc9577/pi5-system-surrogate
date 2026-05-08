# `pi5-system-surrogate` Daemon: Design

## Role

The daemon owns `localhost:6810` on the Pi and presents the same NT4 contract that Limelight's `system-server` presents on real SystemCore. The HAL we built is unmodified upstream — it expects this contract and will hang/crash without it.

Three responsibilities:

1. **Unblock `HAL_Initialize`** by publishing `/Netcomm/Control/ServerReady = true` within 10 s of the HAL starting (else the HAL calls `std::terminate()`).
2. **Bridge SmartIo channels** — subscribe to `/io/{ch}/type` and `/io/{ch}/valset`, drive libgpiod lines, publish `/io/{ch}/valget` for inputs.
3. **Bridge gamepad/enable state** to `mrc::ControlData` — ingest input from a laptop helper and a small web UI, synthesize the protobuf, publish at 5–20 ms cadence with `DsConnected=true` (else the HAL zeros the control word and the robot is permanently disabled).

Plus minor housekeeping: publish `/sys/battery` (else `HAL_GetVinVoltage` returns 0), publish `/imu/*` zeros, drive the RSL GPIO based on enable state.

## Language

**C++26.** Rationale:

- Direct linkage against `libntcore.so` and `libwpiutil.so` we already built — the daemon uses the exact C++ API the HAL itself uses. No FFI layer, no binding mismatch.
- Mirrors the architecture of Limelight's real `system-server` (almost certainly C++), which is what makes our daemon a faithful stand-in.
- Same protobuf-generated bindings the HAL uses for `mrc::*` messages — `MrcComm.proto` compiles once and both sides share the wire format.
- libgpiod ships maintained C++ headers (`gpiod.hpp`) — direct API.
- `-std=c++26` gives us the C++23 niceties the team will see in real WPILib code (`std::expected`, `<print>`, `<flat_map>`) plus the C++26 additions that land in GCC 16: `std::inplace_vector`, `std::optional<T&>`, `std::function_ref`, `std::copyable_function`, plus experimental reflection and contracts.

Tradeoff vs. a Python prototype: more boilerplate, slower iteration. Justified because the daemon is the bridge between the WPILib HAL (C++) and the Pi (C/Linux); writing it in C++ removes a layer of impedance mismatch and keeps the team in the same toolchain as their robot code's native dependencies.

## Build / toolchain

| | Requirement | Apt package |
| --- | --- | --- |
| Compiler | **GCC 16+** with `-std=c++26` (Clang 20+ also works) | `g++-16` from `ppa:ubuntu-toolchain-r/test` on Ubuntu Noble; native on Ubuntu 26.04 |
| Build system | CMake 3.28+ + Ninja | `cmake`, `ninja-build` |
| Protobuf | **nanopb 0.4.9** (the HAL uses nanopb, not stock libprotobuf) | none — runtime sources + pre-generated `.npb.{h,cpp}` are compiled directly from upstream |
| GPIO | libgpiod with C++ header | `libgpiod-dev` |
| HTTP | cpp-httplib (single-header) | vendored as `third_party/httplib.h` |
| Tests | GoogleTest | `libgtest-dev` |

GCC 16.1 was released 2026-04-30 and brings the bulk of usable C++26 (reflection, contracts, the new library types listed above). On Ubuntu Noble — which we use for the workstation HAL build — GCC 16 isn't in the default repos yet; install via the toolchain test PPA:

```bash
sudo add-apt-repository ppa:ubuntu-toolchain-r/test
sudo apt update
sudo apt install g++-16
```

Pi OS Bookworm is in the same boat; either install from a Debian backport that pulls in GCC 16, build it from source, or — since the workstation is already aarch64 — build the daemon natively on the workstation and rsync the binary to the Pi (same pattern we use for the HAL `.so`s).

Link targets: `libntcore.so`, `libwpiutil.so`, `libwpinet.so` from the [hal-port/](../hal-port/) build. Headers live under `hal-port/upstream/allwpilib/{ntcore,wpiutil,wpinet}/src/main/native/include/`. CMake resolves them via a project-local find module (`cmake/FindWPILibNative.cmake`).

```bash
cd daemon
cmake -B build -G Ninja \
  -DCMAKE_CXX_COMPILER=g++-16 \
  -DWPILIB_NATIVE_ROOT=$PWD/../hal-port/upstream/allwpilib
ninja -C build
```

Build targets:

- `pi5-system-surrogate` — daemon binary, installed to `/opt/pi5-system-surrogate/bin/`
- `daemon_tests` — unit tests
- `proto` — auto-generated `MrcComm.pb.{h,cc}` from `proto/MrcComm.proto`

## Module layout

```
daemon/
├── CMakeLists.txt
├── cmake/
│   └── FindWPILibNative.cmake     # resolves .so's + headers from hal-port/
├── proto/
│   └── MrcComm.proto              # copied from upstream allwpilib
├── third_party/
│   └── httplib.h                  # cpp-httplib (vendored)
├── src/
│   ├── main.cpp                   # entrypoint, signal handling, jthread orchestration
│   ├── daemon_state.{cpp,hpp}     # shared state + mutex
│   ├── nt_server.{cpp,hpp}        # nt::NetworkTableInstance bound to localhost:6810
│   ├── ds_bridge.{cpp,hpp}        # /Netcomm/* publishers and subscribers
│   ├── smartio_bridge.{cpp,hpp}   # /io/{ch}/* ↔ gpiod::line
│   ├── imu_publisher.{cpp,hpp}    # /imu/* zero values
│   ├── power_publisher.{cpp,hpp}  # /sys/battery constant
│   ├── rsl.{cpp,hpp}              # blink RSL based on ControlData.Enabled
│   ├── gamepad_ingest.{cpp,hpp}   # subscribe to /dev/gamepad/* topics
│   ├── web_ui.{cpp,hpp}           # cpp-httplib server: enable/disable, mode, e-stop
│   └── pin_map.hpp                # constexpr SmartIo channel ↔ Pi GPIO map
├── tests/                         # GoogleTest cases
└── pi5-system-surrogate.service   # systemd unit
helper-laptop/                     # separate package — runs on driver laptop
└── gamepad_reader.py              # Python is fine here; just an NT4 client
```

The helper laptop side stays Python — it's a small gamepad reader that publishes to NT topics on the daemon's NT instance and isn't in any hot path that benefits from C++.

## Concurrency model

Threads, not coroutines. Simple, and matches what ntcore itself uses internally.

- `main` opens the libgpiod chip, brings up `nt_server`, publishes invariant topics (`ServerReady=true`, initial `ControlData`, `/sys/battery`, `/imu/*`), then spawns:
  - `controldata_publisher` — `std::jthread`, 5–20 ms periodic loop
  - `rsl_blinker` — `std::jthread`, 50 Hz loop
  - `web_ui_listener` — `std::jthread` hosting `cpp-httplib`'s blocking listener
- ntcore's internal thread fires subscriber callbacks; they update shared state and return promptly.
- All shared state lives in `DaemonState`, guarded by a single `std::mutex`. Reads and writes (gamepad input, web UI commands, SmartIo channel mode/value, ControlData fields) take the same lock; the periodic publisher snapshots under it.
- Shutdown propagates via `std::stop_token` from each `jthread`'s automatic stop-source on `SIGTERM`/`SIGINT`.

A single mutex is fine for this workload — contention is bounded (publisher runs at most every 5 ms, callbacks are short, web UI is human-rate). If profiling later shows contention, shard the state.

## NT4 topic contract

All paths and semantics derived from upstream HAL source — see [hal-port/upstream/allwpilib/hal/src/main/native/systemcore/](../hal-port/upstream/allwpilib/hal/src/main/native/systemcore/).

### We publish — robot HAL subscribes

| Topic | Type | Cadence | Purpose |
| --- | --- | --- | --- |
| `/Netcomm/Control/ServerReady` | bool | once at startup, before any HAL connects | unblocks `HAL_Initialize` (10 s timeout) |
| `/Netcomm/Control/ControlData` | bytes (`mrc::ControlData`) | 5–20 ms periodic, `sendAll=true` | enable/disable, mode, alliance, joystick state, watchdog, e-stop |
| `/Netcomm/Control/MatchInfo` | bytes (`mrc::MatchInfo`) | once + on change | match name/type/number — fake values OK |
| `/Netcomm/Control/JoystickDescriptors` | bytes (`mrc::JoystickDescriptors`) | once + on change | gamepad metadata derived from helper |
| `/Netcomm/Control/HasSetWallClock` | bool | once at startup | wall-clock-set indicator |
| `/sys/battery` | double | 1 Hz | constant 12.0 |
| `/imu/rawaccel`, `/imu/rawgyro`, `/imu/quat`, `/imu/euler_*`, `/imu/yaw_*` | double[] | 50 Hz | zero arrays |

`ControlData` invariants — get these wrong and the robot disables itself:

- `ControlWord.DsConnected = true` always (else HAL zeros the word — [FIRSTDriverStation.cpp:710-719](../hal-port/upstream/allwpilib/hal/src/main/native/systemcore/FIRSTDriverStation.cpp#L710-L719))
- `ControlWord.Alliance` is 0–3 on the wire (HAL adds +1 internally)
- Joystick axes are int16 on the wire; HAL normalizes to ±1.0
- `MatchTime` is signed int32 despite the name

### We subscribe — robot HAL publishes

| Topic | Type | Action |
| --- | --- | --- |
| `/io/{0..5}/type` | int | configure libgpiod direction (0=DIO_in, 1=DIO_out, 2=AnalogIn, 3=PwmIn, 4=PwmOut, 5–6=Counter, 13=LED) |
| `/io/{0..5}/valset` | int | drive output GPIO (0 = low, nonzero = high for DIO; PWM µs out of scope) |
| `/io/{0..5}/periodset` | int | PWM output period — out of scope |
| `/io/{0..5}/ledcount`, `ledoffset` | int | addressable LED — out of scope |
| `/Netcomm/Status/HasUserCode` | bool | informational; log only |
| `/Netcomm/Status/HasUserCodeReady` | bool | informational; log only |
| `/Netcomm/Status/CurrentOpModeTrace` | int64 | informational |
| `/Netcomm/Outputs/JoystickOutput/{0..5}` | bytes (`mrc::JoystickOutput`) | rumble/LED feedback — log only for now |
| `/Netcomm/Console/ConsoleLine` | string | log forwarding |
| `/Netcomm/Console/ErrorInfo` | bytes (`mrc::ErrorInfo`) | error logging |
| `/Netcomm/Reporting/LibVersion` | string | log only |
| `/Netcomm/OpModeOptions` | bytes vector | log only |

### We publish — robot HAL subscribes (continued)

| Topic | Type | Cadence | Purpose |
| --- | --- | --- | --- |
| `/io/{0..5}/valget` | int | 50 Hz when channel is input | report digital input value (0 or nonzero) |
| `/io/{0..5}/periodget` | int | when channel is PwmIn | period in µs (out of scope for now) |

### Helper-laptop side — laptop publishes, we subscribe

The gamepad helper is itself an NT4 client to our server. Keeps the wire format consistent end-to-end.

| Topic | Type | Notes |
| --- | --- | --- |
| `/dev/gamepad/{0..5}/axes` | float[12] | normalized ±1.0 |
| `/dev/gamepad/{0..5}/buttons` | uint64 | bitmask, bit 0 = button 1 |
| `/dev/gamepad/{0..5}/povs` | uint8[8] | angle 0–359 or 65535 for off |
| `/dev/gamepad/{0..5}/descriptor` | string (JSON) | name, isGamepad, supportedOutputs |
| `/dev/control/enabled` | bool | from web UI or helper |
| `/dev/control/mode` | int | 1=auto, 2=teleop, 3=test |
| `/dev/control/estop` | bool | latched until reset via web UI |
| `/dev/control/alliance` | int | 0=red1, 1=red2, 2=red3, 3=blue1, … |

The web UI writes the same `/dev/control/*` topics; the daemon reads them as the source of truth and translates into `mrc::ControlData`.

## GPIO pin map (Pi 5 + Waveshare 2-Channel CAN FD HAT)

The HAT consumes these pins (do **not** reuse for SmartIo):

| Pin | Function |
| --- | --- |
| GPIO 7 | SPI CS for MCP251xFD #2 |
| GPIO 8 | SPI CS for MCP251xFD #1 |
| GPIO 9 | SPI MISO |
| GPIO 10 | SPI MOSI |
| GPIO 11 | SPI SCK |
| GPIO 24 | CAN INT line 2 |
| GPIO 25 | CAN INT line 1 |

Tentative SmartIo channel mapping (only 4 of the 6 channels are wired — that's the project scope):

| SmartIo channel | Pi GPIO | Header pin | Use |
| --- | --- | --- | --- |
| 0 | GPIO 17 | 11 | DIO |
| 1 | GPIO 27 | 13 | DIO |
| 2 | GPIO 22 | 15 | DIO |
| 3 | GPIO 23 | 16 | DIO |
| 4 | unused | — | reserved (could be PWM out / analog in later) |
| 5 | unused | — | reserved |

Daemon-only:

| Function | Pi GPIO | Header pin |
| --- | --- | --- |
| RSL | GPIO 26 | 37 |

Pin assignments live in `pin_map.hpp` (constexpr) and are validated against the HAT's pinout; verify before connecting hardware.

## Diagnostics & observability

Three observability surfaces, all cheap to maintain:

### `/healthz` HTTP endpoint

On the web UI port (`:8080`).

- **200 + JSON** when all bridges are healthy:
  ```json
  {
    "status": "ok",
    "uptime_s": 1234.5,
    "controldata": {"last_publish_ms": 4, "cadence_p99_ms": 5.2},
    "smartio": {"channels_configured": 4},
    "rsl": {"last_blink_ms": 12}
  }
  ```
- **503 + JSON** with `reason` field when a bridge is wedged.

For monitoring scripts, browser probes, and CI smoke tests.

### Journald logging

systemd `Type=notify`, `std::println(std::cerr, …)` with structured key=value pairs:

```
level=warn ts=2026-05-07T22:34:18Z component=ds_bridge cadence_p99_ms=8.2 reason=tick_overrun
```

Levels `trace, debug, info, warn, error, fatal`; default `info`; override via `PI5_SURROGATE_LOG=debug`. No third-party logging library — `std::print` carries its weight.

### `/dev/diag/*` NT topics

So the team can point AdvantageScope or `ntcli` at the daemon and see liveness on the same bus they already have open.

| Topic | Type | Notes |
| --- | --- | --- |
| `/dev/diag/heartbeat` | int64 | monotonic, increments every publisher tick |
| `/dev/diag/last_publish_ms` | double | wall time since last successful `ControlData` publish |
| `/dev/diag/cadence_p99_ms` | double | 1 s rolling p99 publisher cadence |
| `/dev/diag/uptime_s` | double | seconds since daemon start |
| `/dev/diag/build_info` | string | `<git_sha> <iso_build_time> <gcc_version>` |

Published at 1 Hz from a dedicated diagnostics task; never under `DaemonState::mutex_`.

## Startup sequence

1. `mlockall(MCL_CURRENT | MCL_FUTURE)` — eliminate page-fault stalls in the publisher
2. Open libgpiod chip (`/dev/gpiochip0` on Pi 5)
3. Bind NT4 server to `localhost:6810`; fail fast if the port is taken
4. Publish `/Netcomm/Control/ServerReady = true`
5. Publish initial `mrc::ControlData` (DISABLED, `DsConnected=true`, no joysticks)
6. Publish `MatchInfo`, `JoystickDescriptors` (empty)
7. Publish `/sys/battery = 12.0`, `/imu/*` zeros
8. Subscribe to `/io/{0..5}/type`, `/io/{0..5}/valset`
9. Subscribe to `/dev/gamepad/*`, `/dev/control/*`
10. Start periodic `ControlData` publisher task (5–20 ms loop), wired to `sd_notify("WATCHDOG=1")` per tick
11. Start RSL blink task (50 Hz, ~1 Hz blink rate when enabled)
12. Start diagnostics task (1 Hz, publishes `/dev/diag/*`)
13. Start web UI on `:8080` (includes `/healthz`)
14. `sd_notify("READY=1")` — systemd marks the unit active
15. Wait for `SIGTERM` / `SIGINT`; handlers trigger `std::stop_source`s, all `jthread`s join, daemon exits cleanly

## Failure modes

| Failure | Behavior |
| --- | --- |
| `/dev/gpiochip0` unavailable | Log + continue without GPIO — HAL still runs, inputs read 0 |
| NT4 bind fails | Fatal, exit non-zero, systemd restarts |
| Robot disconnects | Daemon keeps running, ready for re-connect |
| Laptop helper drops out | Daemon publishes empty `JoystickDescriptors` → robot disables itself |
| Web UI thread throws | Caught at top of `jthread` lambda, logged; supervisor respawns the listener |
| `ControlData` publisher tick > 6 ms (p99) | Log warn with cadence stats; continue |
| `ControlData` publisher tick > 15 ms (single) | Log error; continue (next tick will likely catch up) |
| Publisher hangs > `WatchdogSec=2s` | systemd kills daemon, restarts; robot reconnects when new daemon publishes `ServerReady` |

## Derisking

Three failure modes with engineered responses: protobuf drift, SmartIo channel-mode-flip races, and missed publisher deadlines.

### Protobuf drift between daemon and HAL

The HAL uses **nanopb 0.4.9**, not stock libprotobuf — vendored upstream and committed both as the `.proto` and as pre-generated `.npb.{h,cpp}`. We don't run `protoc`; we compile the same generated files the HAL was built against, plus the nanopb runtime, directly into our binary. Drift is structurally impossible.

```cmake
set(MRC_GEN ${WPILIB_NATIVE_ROOT}/hal/src/generated/main/native/cpp/mrc/protobuf)
set(NANOPB ${WPILIB_NATIVE_ROOT}/wpiutil/src/main/native/thirdparty/nanopb)

target_sources(pi5-system-surrogate PRIVATE
  ${MRC_GEN}/MrcComm.npb.cpp
  ${NANOPB}/pb_common.cpp
  ${NANOPB}/pb_decode.cpp
  ${NANOPB}/pb_encode.cpp
)
target_include_directories(pi5-system-surrogate PRIVATE
  ${WPILIB_NATIVE_ROOT}/hal/src/generated/main/native/cpp
  ${NANOPB}/include
)
```

Cross-checks:

- Build-time assertion that `MrcComm.npb.cpp` exists and is non-empty in the upstream tree. Fails loudly if `hal-port/` is wiped.
- CI round-trip test: encode a `mrc_proto_ProtobufControlData` with our nanopb binding, decode with the same nanopb code, assert every field round-trips. Catches accidental version skew if upstream regenerates with a different nanopb generator.

### SmartIo channel mode flip races

`smartio_bridge` carries a per-channel state machine. State ∈ `{Unconfigured, Input, Output, Pwm, Analog, Counter, AddressableLed}`. Held under per-channel mutex, not the global one.

Rules:

- Every transition is *release current line → request with new flags → update state*, atomic per channel.
- Setting `type` to current state is a no-op (idempotent reconfig).
- `valset` arriving while the channel isn't an output is dropped + logged at debug. HAL re-emits on its next cycle.
- Each transition logs at debug (`channel=2 state=Input→Output`).

Tests:

- Unit: mock `IGpioBackend`; fuzz with random sequences of `(channel, type, valset)` ops, assert state consistency at each step.
- Hardware loopback: wire pin A to pin B, configure A as output and B as input, verify writes propagate. Then flip A to input, verify the line stops driving (catches "left a line driven after release").

### Missed publisher cadence

Publisher holds `DaemonState::mutex_` for memcpy-equivalent work only. Protobuf encoding and `nt::Publish` happen on a snapshot, lock-free.

Architectural rule: *no I/O, no logging, no allocation, no syscalls under the daemon mutex.* Code-review enforced; clang-tidy can flag obvious violations.

Runtime mitigations:

- `mlockall(MCL_CURRENT | MCL_FUTURE)` at startup → no page-fault stalls.
- Publisher thread runs `SCHED_FIFO` priority 10 via systemd `CPUSchedulingPolicy=fifo`. Low FIFO priority — well below the kernel RT threshold, well above `SCHED_OTHER`.
- 1 s rolling p50/p99/p999 cadence tracked in-process; warn at p99 > 6 ms, error on any single tick > 15 ms.
- `sd_notify("WATCHDOG=1")` every publisher tick. systemd unit `WatchdogSec=2s`. Hung publisher → systemd restart → robot reconnects to the new daemon when it publishes `ServerReady`.

CI:

- `bench_publisher` microbenchmark runs 10 000 iterations of snapshot + encode + publish against a fake NT instance, asserts p99 < 1 ms on workstation hardware. Regressions fail the build.

## systemd integration

```ini
# /etc/systemd/system/pi5-system-surrogate.service
[Unit]
Description=SystemCore stand-in NT4 daemon
After=network-online.target sys-subsystem-net-devices-can_s0.device
Wants=network-online.target

[Service]
Type=notify
ExecStart=/opt/pi5-system-surrogate/bin/pi5-system-surrogate
Environment=LD_LIBRARY_PATH=/opt/pi5-system-surrogate/lib
Restart=on-failure
RestartSec=2
WatchdogSec=2s
CPUSchedulingPolicy=fifo
CPUSchedulingPriority=10
LimitMEMLOCK=infinity

[Install]
WantedBy=multi-user.target
```

`Type=notify` lets `sd_notify("READY=1")` mark the unit active only after `ServerReady` is published. `WatchdogSec=2s` paired with per-tick `sd_notify("WATCHDOG=1")` auto-restarts a hung publisher. `LimitMEMLOCK=infinity` is required for `mlockall`.

The robot service unit (separate, in `robot-java/`) should `After=pi5-system-surrogate.service` so the HAL doesn't time-out waiting for `ServerReady` on cold boot.

## Testing

GoogleTest, run via `ninja -C build test` or directly as `./build/daemon_tests`.

- **Unit:** protobuf round-trip for every `mrc::*` message; `ControlData` state-transition table; SmartIo channel state machine using a `IGpioBackend` interface so libgpiod can be swapped for a fake in tests
- **Integration:** spin up the daemon in-process, attach a `nt::NetworkTableInstance` client, verify (a) `ServerReady=true` appears within 100 ms of bind, (b) `ControlData` cadence ≤ 20 ms, (c) `DsConnected` is always set
- **Hardware loopback:** wire two Pi GPIO pins together; confirm DIO write on one channel reads back as input on another (end-to-end check on the libgpiod bridge)
- **Smoke test:** real `robot.jar` + real Phoenix 6 motor → enable from web UI → motor spins. The Phase 6 milestone in the project plan.

## Out of scope

- Real FIRST DS wire protocol
- PWM (use Phoenix 6 / REVLib over CAN for motors instead)
- Analog input
- Addressable LED
- Real IMU data
- USB cameras
- Power distribution telemetry beyond a fake battery voltage
- FMS connection (we publish `FmsConnected=false` always)
