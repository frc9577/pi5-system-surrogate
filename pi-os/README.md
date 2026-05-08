# Pi OS configuration

Files that get applied to the Raspberry Pi to make its hardware look like a
SystemCore from the HAL's perspective. Two concerns: the CAN bus naming the
HAL expects, and the FRC-standard `10.TE.AM.x` networking the team uses.

## Files

| File | Purpose |
| --- | --- |
| `90-frc-can.rules` | udev rule renaming the kernel default `can0` to `can_s0` so WPILib's SystemCore HAL CAN code finds the interface. |
| `80-can_s0.network` | systemd-networkd config bringing `can_s0` up at FRC's standard 1 Mbps classic CAN bitrate. |
| `50-frc-eth.network.tmpl` | systemd-networkd **template** for onboard Ethernet. Rendered at boot from the team-number file. |
| `frc-network-render` | Boot-time script. Reads `/boot/firmware/frc-team-number`, renders the template into `/run/systemd/network/50-frc-eth.network`, and sets the hostname. |
| `frc-network.service` | systemd oneshot that runs `frc-network-render` before `systemd-networkd` starts. |
| `team-number.example` | Example contents for `frc-team-number` (single integer, 1..9999). |

## Team-number-driven networking

FRC convention: a team number maps to a `/24` subnet `10.TE.AM.0/24` where
`TE` and `AM` are the high and low two digits of the team number, zero-padded.
For team 1234 that yields `10.12.34.0/24`, with the controller at `.2`, the
radio/gateway at `.1`, and the driver station at `.5`. Hostname follows the
roboRIO pattern: `systemcore-{team}-frc`.

The team number lives in a single file on the boot partition:

```
/boot/firmware/frc-team-number   # contents: just an integer like "1234"
```

Editing it is a one-file change you can make from any computer that mounts
the SD card. After editing, reboot — or:

```bash
sudo systemctl restart frc-network.service
sudo systemctl reload systemd-networkd
```

**Fallback to DHCP.** If the file is missing, empty, or contains an invalid
team number, `frc-network-render` writes a DHCP `[Network]` block instead
of the static FRC address. The Pi still comes up on whatever network it's
plugged into, and the cause is logged to the journal — so a typo in the
team file doesn't leave the box unreachable. Hostname is left alone in
the fallback path.

## Install

```bash
# CAN
sudo cp 90-frc-can.rules    /etc/udev/rules.d/
sudo cp 80-can_s0.network   /etc/systemd/network/

# Network template + render service
sudo install -d /etc/frc
sudo cp 50-frc-eth.network.tmpl /etc/frc/
sudo install -m 0755 frc-network-render /usr/local/sbin/
sudo cp frc-network.service /etc/systemd/system/
sudo systemctl enable frc-network.service

# Team number — edit to your team's number
echo 9999 | sudo tee /boot/firmware/frc-team-number > /dev/null

# Apply
sudo udevadm control --reload && sudo udevadm trigger
sudo systemctl enable --now systemd-networkd
sudo systemctl start frc-network.service
sudo systemctl reload systemd-networkd

# Verify
ip -details link show can_s0
ip -4 addr show end0   # or eth0
hostname
```

## Kernel / boot config

The Waveshare 2-Channel CAN FD HAT uses MCP2517FD/MCP2518FD chips on SPI.
Add the device-tree overlay to `/boot/firmware/config.txt`:

```
dtparam=spi=on
dtoverlay=mcp251xfd,spi0-0,interrupt=25
```

(Adjust pin number per your HAT's wiring; 25 is the Waveshare default for
channel 0.)

The `mcp251xfd` driver is in the mainline kernel — no out-of-tree build is
required on a recent Raspberry Pi OS image.

## Notes

- We deliberately match real SystemCore conventions: the same `can_s*` /
  `can_d*` interface naming, the same `10.TE.AM.2` static address, an
  analogous mDNS hostname. The team builds correct muscle memory and our
  daemon stays a drop-in.
- `LinkLocalAddressing=no` on the Ethernet config prevents a 169.254.x.x
  fallback when the radio is absent — keeps `arp` and mDNS unambiguous on
  the bench.
- Pi OS sometimes names the onboard Ethernet `end0` (BCM2712 driver) rather
  than `eth0`. The `[Match]` block accepts either.
