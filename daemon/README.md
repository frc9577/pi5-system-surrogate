# Driver Station surrogate

A daemon that publishes the NetworkTables 4 protobuf topics the WPILib
SystemCore HAL's `FIRSTDriverStation.cpp` subscribes to, standing in for
the closed "system server" component of real SystemCore.

## Topics it publishes

- `mrc::ControlData` — enable/disable, mode (auto/teleop/test/disabled),
  alliance, e-stop, joystick axes / buttons / POVs
- `mrc::MatchInfo` — match number, type, event name
- Joystick descriptor topics

## Topics it subscribes to

- Joystick output topics — rumble, LED, trigger feedback (publishes back to
  the laptop's gamepad)

## Architecture

```
[Laptop gamepad] ── SDL2/evdev ──► [Surrogate process] ── NT4 ──► [Pi: robot code]
                                          ▲
                                          │
                                  [Tiny web UI:
                                   enable / mode]
```

Runs on the laptop, reaches the Pi over Ethernet. Robot code can't tell
the surrogate apart from a real system server.

## Plan

Language TBD. Python with `pyntcore` is the fastest path to a working
prototype; we may rewrite in Java later for fidelity with WPILib's NT4
client behavior.

## Status

Not started. First milestone: publish a static "disabled" `mrc::ControlData`
and confirm a stub robot reads it.
