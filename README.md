# LODESTAR CAIRN (ALPHA)

Crisis-use field kit firmware and dashboard for MeshCore, by FEAR REACTIVE.
CAIRN turns a Heltec V4 R2 companion into a **station**: it gets a permanent ID (`LC-xxxxx`), keeps a journal of **everything it hears**, sends a one-button check-in with GPS, and feeds a tablet dashboard that works with **no internet**.

**Status: ALPHA. The firmware compiles and passes its tests, and the dashboard is tested with practice data, but neither has run on real hardware or on the tablet yet.**

## The kit
Android tablet (keyboard, mouse, stylus) + USB hub + two power banks + solar panel + Heltec V4 R2 running CAIRN (companion) + XIAO ESP32S3 running plain PULSAR (repeater, powered from the hub only).
The companion connects to the tablet two ways: **Bluetooth** to the MeshCore app (chat) and **USB** to the dashboard (status and log).

## What it does
- **Station ID** `LC-xxxxx` (random, no confusing I/O/0/1), made at first boot, kept through reboots.
- **Check-in** button sends `LC-xxxxx Reporting at: <lat>,<lon>` (or `no GPS fix`) to a channel.
- **Journal**: every message, advert (repeaters, companions, room servers), check-in and boot, stored on the radio (`/cairn.log`, rotated at 192 KB) and copied to the tablet's browser. Export CSV or JSON lines.
- **Disaster mode**: screens off sooner, quiet hours off, stale repeaters marked sooner.
- **Station page**: REACHABLE / NO REPEATER HEARD, last usable repeater, last place with coverage, GPS, clock, counts.
- **Nearby**: repeaters, room servers, companions with SNR, hops, age, distance and bearing.
- **Check-ins roster**: everyone who reported, overdue flags, tap to mark NEEDS HELP.
- **Map**: offline vector maps (PMTiles), your position, nodes, check-ins.
- **Power**: radio battery if fitted, tablet battery, manual power-bank timers.
- **Guide**: offline field guide, printable.
- **No on-screen keyboard** unless you tap the keyboard icon.

## Offline rule
Online is fine **before** you go: load the dashboard once, load an offline map. In the field nothing needs internet. The dashboard is a PWA (cached after first load) and also ships as `dashboard.html` in each release.

## Honest limits
- The radios cannot see power-bank or tablet charge; Power tab timers are reminders.
- Keep the two antennas apart and away from the hub; check `stats-radio` noise floor if range is poor.
- Web Serial in Chrome for Android (v148+) is new: test on your tablet.
- Mesh radio is not a replacement for 911, phones or satellite messengers.

## Build and flash
Actions → "Build LODESTAR CAIRN" → Run workflow (env `heltec_v4_companion_radio_ble`, units, publish + new tag). Flash `firmware-merged.bin` at https://flasher.meshcore.io (Custom Firmware). Heltec V4 **R2** only.
Use the **BLE** build: the USB port carries the dashboard. (The USB build uses the port for the companion protocol and has no dashboard.)

## Commands (serial or MeshCore CLI)
`cairn` status · `cairn id` · `cairn ok [channel]` · `cairn mode disaster|normal` · `cairn dump` · `cairn clear yes` · `cairn time <epoch>`; all NOVA commands also work.

## Layout
`cairn/` firmware logic and tests · `nova/` vendored NOVA · `tools/apply_cairn.py` patches MeshCore · `dashboard/cairn.html` source · `tools/build_dashboard.py` builds `docs/index.html` (commit the result; CI checks it).

## Dashboard hosting
Enable GitHub Pages (Settings → Pages → `main` / `/docs`) then open the page once on the tablet and "Add to Home screen".

Changelog: 0.1.0-alpha first build.
