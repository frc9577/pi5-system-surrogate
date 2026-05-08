# pi5-system-surrogate

A development analog of the FRC 2027 SystemCore control system, built on a
Raspberry Pi 5 + Waveshare 2-Channel CAN FD HAT, using the upstream
WPILib HAL unmodified. The team writes real WPILib 2027 robot code today;
when SystemCore hardware ships, that code transfers unchanged.

This repository is the **platform** — the daemon that stands in for
Limelight's `system-server`, the Pi OS configs, the build wrapper for
upstream WPILib, the virtual-hardware emulation harness, and the
contract-validation client. Robot code lives in a sister repo,
`frc-2027-robot-starter`.

## Layout

```
daemon/            C++26 daemon — NT4 server on localhost:6810
pi-os/             udev + systemd-networkd + FMS networking config
hal-port/          build wrapper for upstream allwpilib (submodule)
dev-emulation/     gpio-sim + vcan harness; integration test orchestrators
sample-robot/      contract-validation NT4 client (platform smoke test)
docs/              architecture, design, derisking notes
```

## Build

Two commands, in order, from the repo root after a fresh clone:

```bash
git clone --recurse-submodules <this-repo-url> && cd pi5-system-surrogate

# 1) Build upstream WPILib (Java jars + JNI .so's the daemon links against)
./hal-port/build.sh :ntcore:jar :wpiutil:jar \
                    :ntcore:ntcoreJNISharedReleaseSharedLibrary \
                    :wpiutil:wpiutilJNISharedReleaseSharedLibrary \
                    :hal:halJNISharedReleaseSharedLibrary \
                    -Ponlylinuxsystemcore

# 2) Build the daemon
(cd daemon && cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=g++-16 && \
 ninja -C build && (cd build && ctest))
```

Prereqs: `g++-16` (via `ppa:ubuntu-toolchain-r/test`), `cmake`,
`ninja-build`, `libgpiod-dev`, JDK 25, internet access for FetchContent
of GoogleTest + cpp-httplib on first configure.

## Run

```bash
./daemon/build/pi5-system-surrogate
# NT4 :6810  +  Web UI :8080
```

`./daemon/build/check_server_ready` confirms the daemon serves the
`HAL_Initialize` contract: `ServerReady=true` + `ControlData` with
`DsConnected=true`.

## End-to-end test

For the deploy / reload / match-phase loop, clone the robot template as
a sibling directory:

```bash
cd ..
git clone <robot-template-url> frc-2027-robot-starter
cd pi5-system-surrogate
./dev-emulation/run-match-test.sh
```

For virtual-hardware (libgpiod + CAN) testing without a Pi:

```bash
sudo ./dev-emulation/run-integration-test.sh
```

## Architecture

See [docs/architecture.md](docs/architecture.md) for the overall plan,
[docs/systemcore-vs-pi5.md](docs/systemcore-vs-pi5.md) for what's faked
vs. real, and [docs/daemon-design.md](docs/daemon-design.md) for the
daemon's internal design + the NT4 contract.

## License

[MIT](LICENSE) for code in this repo. See [NOTICE.md](NOTICE.md) for the
licensing of dependencies (BSD-3 upstream WPILib, zlib nanopb, MIT
cpp-httplib, BSD-3 GoogleTest).
