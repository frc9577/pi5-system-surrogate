# HAL port

Working build of WPILib's SystemCore HAL on aarch64 Linux.

## Status

**Working** — the upstream SystemCore HAL builds unchanged on aarch64 hosts
(Ubuntu Noble + Pi OS Bookworm). No source patches needed.

## Why no SmartIo substitute is needed

Earlier planning assumed `SmartIo.cpp` directly drove an RP2350 coprocessor and
would need replacing for Pi 5. Reading the actual source showed that's wrong:
SmartIo is a **NetworkTables 4 client**. It publishes/subscribes to topics on
`localhost:6810` (the system-server NT port) like:

- `/io/{channel}/type` — mode (DigitalInput/Output, PWM, AnalogInput, …)
- `/io/{channel}/valset` — value to write (outputs)
- `/io/{channel}/valget` — value to read (inputs)
- `/io/{channel}/periodset`, `/io/{channel}/periodget` — PWM period
- `/io/{channel}/ledcount`, `/io/{channel}/ledoffset` — addressable-LED params

On real SystemCore, a system-server daemon bridges those NT topics to the
RP2350. On our Pi 5, our DS surrogate / system-server daemon will bridge them
to libgpiod (and to userspace controls for the RSL). The HAL itself doesn't
care.

This means:
- The **HAL is portable as-built** — same `.so` for SystemCore and our Pi.
- `CAN.cpp` is also unchanged — it talks SocketCAN on `can_s0..can_s4` regardless.
- `FIRSTDriverStation.cpp` is also unchanged — it's NT4 protobuf the whole way down.

## Vendor library compatibility (verified)

Both Phoenix 6 (`25.90.0-alpha-2`) and REVLib (`2027.0.0-alpha-1`) ship native
binaries for the `linuxsystemcore` platform classifier. Robot Java code on the
Pi will use those vendor JNI artifacts plus the `libwpiHal.so` we build here.

## Prerequisites

```
sudo apt install -y openjdk-25-jdk-headless libgpiod-dev libgpiod2 \
                    ninja-build python3 patch
```

Disk: clone is ~150 MB; build outputs add ~1 GB; downloaded toolchain adds
~500 MB.

## Build

```
export JAVA_HOME=/usr/lib/jvm/java-25-openjdk-arm64
export PATH=$JAVA_HOME/bin:$PATH

# One-time: install the SystemCore cross-toolchain bundle
./build.sh installSystemCoreToolchain -Ponlylinuxsystemcore

# Build the HAL JNI shared lib (and its transitive deps: wpiutil, ntcore,
# wpinet, datalog)
./build.sh :hal:halJNISharedReleaseSharedLibrary -Ponlylinuxsystemcore
```

`build.sh` is a thin wrapper around `upstream/allwpilib/gradlew` that injects
[`gradle/toolchain-override.gradle`](gradle/toolchain-override.gradle) via
`--init-script`. All other arguments pass straight through to gradle.

`-Ponlylinuxsystemcore` constrains the build to a single platform target so
task names lose the platform suffix and the SystemCore toolchain is required
(non-optional).

## Toolchain override (no source patches)

The upstream allwpilib submodule worktree is unmodified. The one
build-time override we need — pointing the opensdk toolchain tag at
`v2025-2` instead of the default `v2025-1` (which is missing the
aarch64-host SystemCore bundle and 404s on download) — lives in
[`gradle/toolchain-override.gradle`](gradle/toolchain-override.gradle) and
is applied via Gradle's `--init-script` mechanism by `build.sh`. The
override sets `toolchainTag` on the `systemcoreToolchain` and
`linuxarm64Toolchain` extensions in an `afterEvaluate` hook on every
project, equivalent to:

```groovy
extensions.findByName('systemcoreToolchain').toolchainTag.set('v2025-2')
extensions.findByName('linuxarm64Toolchain').toolchainTag.set('v2025-2')
```

When upstream opensdk's default tag advances past the missing-bundle
release, this override can be deleted.

## Build artifacts (verified)

| Artifact | Path | Size | Format |
| --- | --- | --- | --- |
| HAL | `hal/build/libs/hal/shared/release/libwpiHal.so` | 739 KB | ELF aarch64 |
| HAL JNI bindings | `hal/build/libs/halJNIShared/shared/release/libwpiHaljni.so` | 466 KB | ELF aarch64 |
| NetworkTables | `ntcore/build/libs/ntcore/shared/release/libntcore.so` | — | ELF aarch64 |
| WPIUtil | `wpiutil/build/libs/wpiutil/shared/release/libwpiutil.so` | — | ELF aarch64 |
| WPINet | `wpinet/build/libs/wpinet/shared/release/libwpinet.so` | — | ELF aarch64 |
| DataLog | `datalog/build/libs/datalog/shared/release/libdatalog.so` | — | ELF aarch64 |

Symbol-level verification of `libwpiHal.so`:

- 1865 exported symbols
- `HAL_GetSystemServerHandle`, `wpi::hal::GetSystemServer`,
  `wpi::hal::ShutdownSystemServer` — present, confirms the SystemCore HAL
  variant (not sim)
- `HAL_CAN_SendMessage`, `HAL_InitializeDIOPort`,
  `HAL_GetJoystick{Axes,Buttons,POVs,Descriptor}` — present
- Runtime deps: `libwpiutil.so`, `libntcore.so`, `libdatalog.so`,
  `libwpinet.so` (all built as siblings) plus `libstdc++/libm/libgcc_s/libc`

## Toolchain notes

`./build.sh installSystemCoreToolchain` downloads
`arm64-bookworm-2025-aarch64-bookworm-linux-gnu-Toolchain-12.2.0.tgz` from
`wpilibsuite/opensdk` v2025-2 and installs it under
`~/.gradle/toolchains/first/2025/systemcore`. It's a GCC 12.2 toolchain with a
Debian Bookworm aarch64 sysroot — the same toolchain the WPILib team uses to
build their official SystemCore artifacts.

On a non-aarch64 host the same Gradle commands will pull a different toolchain
variant (e.g. `arm64-bookworm-2025-x86_64-linux-gnu-Toolchain-12.2.0.tgz`) and
cross-compile.

## Next steps

1. Write the system-server / DS surrogate that publishes the `/io/{ch}/*` NT
   topics SmartIo subscribes to, bridged to libgpiod.
2. Generate a WPILib 2027 alpha Java robot project that links against these
   `.so`s.
