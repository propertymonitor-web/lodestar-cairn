# LODESTAR CAIRN (ALPHA) - Ver. 1.0.1 (release cairn-alpha2)

Crisis-use field kit firmware and dashboard for MeshCore, by FEAR REACTIVE.
CAIRN turns a Heltec V4 R2 companion into a **station**: it gets a permanent ID (`LC-xxxxxxxx`), keeps a journal of **everything it hears**, sends a one-button check-in with GPS, and feeds a tablet dashboard that works with **no internet**.

**Status: ALPHA. The firmware compiles and passes its tests, and the dashboard is tested with practice data, but neither has run on real hardware or on the tablet yet.**

## What changed in cairn-alpha2 (Ver. 1.0.1)
- **The station ID is now 8 characters and comes from the radio's own public key** (it was 5 random characters in cairn-alpha1, Ver. 0.1.0). Example: `LC-K7M2P4XQ`.
- Why: this system is offline, so an ID can never be checked against a database for duplicates. 5 characters (33.5 million combinations) is fine for a small group but starts to collide at event scale (about 1 in 67 for 1,000 stations). 8 characters (1.1 trillion combinations) makes a duplicate essentially impossible, and because it comes from the radio's key it needs no stored file, can always be rebuilt, and matches the radio's advert so a check-in can be traced to its node.
- The dashboard still reads old 5-character check-ins, so alpha1 and alpha2 stations can share a mesh.
- The version number is now **1.0.1**.
- If you flashed alpha1, you will see a new ID after updating. Nothing else needs doing.

## The station ID
- Format `LC-` plus 8 letters/digits. The 32 characters used leave out I, O, 0 and 1 so it is hard to misread aloud.
- It is taken from the **last 5 bytes (40 bits) of the radio's public key**. MeshCore creates that key randomly the first time the radio starts, and keeps it through reboots and firmware updates. So the ID is **random at first boot, never changes, and survives reboot**. It changes only if the radio's identity is erased or regenerated (a full erase, or the MeshCore "new identity" action).
- **It is not checked against anything.** Offline, nothing can tell two radios apart if they share an ID. The chance of that is tiny but not zero:

| Stations in range | Chance any two share an ID (8 chars) | (5 chars, old) |
|---|---|---|
| 20 | about 1 in 5 billion | about 1 in 176,000 |
| 100 | about 1 in 220 million | about 1 in 6,800 |
| 1,000 | about 1 in 2.2 million | about 1 in 67 |
| 5,000 | about 1 in 90,000 | about 1 in 3 |

- The ID identifies a station, it is not a secret and not a login: the public key it comes from is broadcast by the radio anyway.
- If two people ever do report the same ID, the check-in position and the sender name in the Check-ins list will still tell them apart.

## The kit
Android tablet (keyboard, mouse, stylus) + USB hub + two power banks + solar panel + Heltec V4 R2 running CAIRN (companion) + XIAO ESP32S3 running plain PULSAR (repeater, powered from the hub only).
The companion connects to the tablet two ways: **Bluetooth** to the MeshCore app (chat) and **USB** to the dashboard (status and log).

## What it does
- **Station ID** `LC-xxxxxxxx`: see "The station ID" below.
- **Check-in** button sends `LC-xxxxxxxx Reporting at: <lat>,<lon>` (or `no GPS fix`) to a channel.
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
`cairn` status (shows ID and version) · `cairn id` · `cairn ok [channel]` · `cairn mode disaster|normal` · `cairn dump` · `cairn clear yes` · `cairn time <epoch>`; all NOVA commands also work.

## Layout
`cairn/` firmware logic and tests · `nova/` vendored NOVA · `tools/apply_cairn.py` patches MeshCore · `dashboard/cairn.html` source · `tools/build_dashboard.py` builds `docs/index.html` (commit the result; CI checks it).

## Dashboard hosting
Enable GitHub Pages (Settings → Pages → `main` / `/docs`) then open the page once on the tablet and "Add to Home screen".

Changelog:
- 1.0.1 (cairn-alpha2): 8-character station ID from the radio's public key; version set to 1.0.1; dashboard reads 5- and 8-character check-ins.
- 0.1.0-alpha (cairn-alpha1): first build, 5-character random ID.

Before going in the field, run through `CAIRN-TEST-CHECKLIST.txt`.
