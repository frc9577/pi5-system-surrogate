# sample-robot

Minimal Java NT4 client that validates the HAL contract
`pi5-system-surrogate` serves. Stands in for what `HAL_Initialize()` checks
on the wire, without dragging in the full WPILib HAL.

## What it tests

1. `/Netcomm/Control/ServerReady = true` appears on `localhost:6810`
   within 4 seconds. Otherwise `HAL_Initialize()` would terminate after 10s.
2. `/Netcomm/Control/ControlData` (raw bytes, type-string
   `proto:mrc.proto.ProtobufControlData`) is being published with the
   `DsConnected` bit set in `ControlWord`. Otherwise
   `HAL_RefreshDSData()` zeros the control word and the robot
   permanently disables itself.

Exit code 0 on pass, 1 on any timeout or contract violation.

## Run

You need a JDK 17+ and Gradle 8+ on the path (the project doesn't ship a
gradle wrapper; standard `gradle` works).

```bash
# In another terminal, start the daemon:
( cd ../daemon && ./build/pi5-system-surrogate )

# In this directory:
gradle run
# ...or with a custom host:
gradle run --args="pi.local"
```

Sample passing output:

```
[PASS] ServerReady=true (after 50 ms)
[PASS] ControlData received (2 bytes, ControlWord=0x20, DsConnected=true, Enabled=false) after 100 ms
```

## Build a fat jar

Bundles all WPILib runtime into one artifact:

```bash
gradle fatJar
java -jar build/libs/sample-robot-all.jar
```

## Adjusting for your platform

`build.gradle.kts` defaults to:

- `wpilibVersion = "2027.0.0-alpha-5"` — bump as new alphas/betas land,
  see https://github.com/wpilibsuite/SystemcoreTesting for the current
  release tag.
- `nativeClassifier = "linuxarm64"` — works on the aarch64 workstation
  used to build the daemon. For real SystemCore hardware (CM5 + RP2350)
  switch to `linuxsystemcore`. For x86_64 dev hosts: `linuxx86-64`.

The Maven artifact paths follow:

```
edu.wpi.first.ntcore:ntcore-java:<version>            # pure-Java sources
edu.wpi.first.ntcore:ntcore-jni:<version>:<classifier>  # native loader
```

If a particular alpha/classifier combination isn't published yet,
`gradle` will report an unresolved dependency — pick a different
classifier or version.
