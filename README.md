# LODESTAR CAIRN (ALPHA) - Ver. 1.2.0 (release cairn-alpha7)

![CAIRN emblem](docs/cairn-logo.png)


Crisis-use field kit firmware and dashboard for MeshCore, by Wolf Reactive Systems (WRS).
CAIRN turns a Heltec V4 R2 companion into a **station**: it gets a permanent ID (`LC-xxxxxxxx`), keeps a journal of **everything it hears**, sends a one-button check-in with GPS, and feeds a tablet dashboard that works with **no internet**.

**Status: ALPHA. The firmware compiles and passes its tests, and the dashboard is tested with practice data, but neither has run on real hardware or on the tablet yet.**

## What changed in cairn-alpha7 (Ver. 1.2.0): the CAIRN Station tablet app
- **Android app "CAIRN Station"** (`android/`, built by GitHub Actions, attached to the release as `cairn-station-1.2.0.apk`): the dashboard inside an app that can be the tablet's Home screen. Start-up steps: splash, **Wi-Fi (the only skippable step)**, **repeater over the USB cable (required)**, **companion radio over Bluetooth (required)**, then the dashboard. If either link drops later, the steps return.
- **Firmware: CAIRN text over Bluetooth.** The radio now answers `nova`/`cairn` commands and streams its journal and status over the Bluetooth companion link (one extra frame type, 0xC8), not only over USB. The USB path is unchanged. **The tablet app needs firmware 1.2.0.**
- **Files screen:** the day's journal and repeater logs, saved reports, downloads; view, send by email, copy to Download/CAIRN, delete.
- **Repeater card** on the Station screen: the repeater's own text, status requests every 30 s, send a command, set its clock.
- **Online screen (Wi-Fi extras):** official US weather forecast and alerts (NWS), earthquakes (USGS), update check and download, offline-map download straight to the tablet, "Get ready now" button, and **Ask Gemini** (greyed out with no internet). Last answers stay on screen, marked with their age, when Wi-Fi is gone.
- See `android/README.md` for install steps and the honest list of what is not tested or not included. The app has not run on real hardware yet.

## What changed in cairn-alpha6 (Ver. 1.1.0)
- **Easy view for non-technical people:** a new Home screen with big buttons: **I'M OK**, **I NEED HELP** (asks first, repeats until cancelled), one-tap **status** (need water/medical/food/shelter/transport, evacuating, staying put, can help) and **mark this place** (water, shelter, medical aid, hazard, meeting point, road blocked, supplies). Full view still has the radar, nearby list, log and console.
- **Add / remove channels from the dashboard** (and by command): open `#name` channels, private channels with a made key, join with a key, show key, remove, pick the check-in channel. Up to 40.
- **Standard and Disaster modes with a real power plan:** both use the highest transmit power the US unlicensed rule (47 CFR 15.247) allows for your antenna; Standard saves battery when it runs low, Disaster never does. Settings are remembered (`/cairn.cfg`). There is no higher legal power for unlicensed radios in an emergency (see FEATURES file).
- **Power and solar:** battery saving, low-battery message, automatic I'M OK, a "how long will my power last?" calculator for power banks, tablet and solar, radio power shown in mW.
- **Alerts:** a red alert with sound on every screen when another station calls for help, with distance, direction and "Show on map"; People and Map show help calls, statuses and marked places; one-tap **situation report**.
- **Radio screen:** new HELP page (two presses to send) and POWER page (switch mode).
- **Splash screen** for the tablet that scales to any screen (designed for 1280x800).
- **New field guide:** plain language, large type, a one-page quick-start card; on screen, printable, downloadable (`docs/CAIRN-Field-Guide.html` / `.pdf`, `docs/CAIRN-Quick-Start-Card.pdf`).
- New tests: firmware logic (including SHA-256 and `#channel` keys), screens, and the dashboard in a real browser at tablet, portrait and phone sizes.

## What changed in cairn-alpha5 (Ver. 1.0.4)
- **Chat from the dashboard:** a new Chat tab to send and read messages (channels and direct messages, 140 characters). New commands `cairn say`, `cairn dm`, `cairn chans`.
- Sent messages are stored in the journal with everything else.
- **About "repeater":** the CAIRN radio is a companion and does not repeat. The kit's second radio (PULSAR) is the repeater; together they are the communications hub.

## What changed in cairn-alpha4 (Ver. 1.0.3)
- The boot splash stays up **5 seconds** (was 3) and shows four short lines beside the emblem instead of five, so it can be read at a glance.

## What changed in cairn-alpha3 (Ver. 1.0.2)
- **OLED:** a new boot splash with the CAIRN emblem (a guiding star over a cairn of stones) and a new **CAIRN STATION** screen (ID, journal size, last check-in; press Enter or hold the button to send a check-in).
- Version is now **1.0.2**.

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
| 20 | about 1 in 6 billion | about 1 in 176,000 |
| 100 | about 1 in 220 million | about 1 in 6,800 |
| 1,000 | about 1 in 2.2 million | about 1 in 67 |
| 5,000 | about 1 in 90,000 | about 1 in 3 |

- The ID identifies a station, it is not a secret and not a login: the public key it comes from is broadcast by the radio anyway.
- If two people ever do report the same ID, the check-in position and the sender name in the Check-ins list will still tell them apart.

## The kit
Android tablet (keyboard, mouse, stylus; designed for a 1280x800 10-inch screen, scales to others) + USB hub + two power banks + solar panel + Heltec V4 R2 running CAIRN (companion) + XIAO ESP32S3 running plain PULSAR (repeater, powered from the hub only).
In a browser, the companion connects to the tablet two ways: **Bluetooth** to the MeshCore app (chat) and **USB** to the dashboard (status and log). In the **CAIRN Station** tablet app (firmware 1.2.0) the dashboard uses **Bluetooth** for the companion and the **USB cable goes to the repeater** instead; the radio talks to one Bluetooth app at a time.

## What it does
- **Station ID** `LC-xxxxxxxx`: see "The station ID" below.
- **Check-in** button sends `LC-xxxxxxxx Reporting at: <lat>,<lon>` (or `no GPS fix`) to a channel.
- **Journal**: every message, advert (repeaters, companions, room servers), check-in and boot, stored on the radio (`/cairn.log`, rotated at 192 KB) and copied to the tablet's browser. Export CSV or JSON lines.
- **Disaster mode**: screens off sooner, quiet hours off, stale repeaters marked sooner.
- **Chat**: send and read channel and direct messages from the tablet.
- **Station page**: REACHABLE / NO REPEATER HEARD, last usable repeater, last place with coverage, GPS, clock, counts.
- **Nearby**: repeaters, room servers, companions with SNR, hops, age, distance and bearing.
- **Check-ins roster**: everyone who reported, overdue flags, tap to mark NEEDS HELP.
- **Map**: offline vector maps (PMTiles), your position, nodes, check-ins.
- **Power**: radio battery if fitted, tablet battery, manual power-bank timers.
- **Guide**: offline field guide, printable.
- **No on-screen keyboard** unless you tap the keyboard icon.
- **OLED**: emblem splash and a station screen with a check-in button.

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
`cairn` · `id` · `ok` · `help [off]` · `beacon` · `ustatus` · `mark` · `mode standard|disaster` · `power full|saver|status` · `antenna <dBi>` · `auto` · `eco` · `lowbat` · `main` · `chans` · `chan add|new|key|remove` · `say` · `dm` · `config` · `dump` · `clear yes` · `time`. Full list in `CAIRN-FEATURES-AND-COMMANDS.txt`; all NOVA commands also work.

## Layout
`cairn/` firmware logic, OLED screens (`cairn/ui`) and tests · `tools/make_logo.py` draws the emblem · `nova/` vendored NOVA · `tools/apply_cairn.py` patches MeshCore · `dashboard/cairn.html` source · `tools/build_dashboard.py` builds `docs/index.html` and `tools/build_guide.py` builds the stand-alone guide (commit the results; CI checks them); `dashboard/guide.html` is the field guide source.

## Dashboard hosting
Enable GitHub Pages (Settings → Pages → `main` / `/docs`) then open the page once on the tablet and "Add to Home screen".

Changelog:
- 1.2.0 (cairn-alpha7): CAIRN Station Android tablet app, CAIRN text over Bluetooth, Files, repeater card, Online (weather, earthquakes, updates, map download, Gemini).
- 1.1.0 (cairn-alpha6): Easy view, help/status/mark messages, channel add/remove, Standard/Disaster power plan, power calculator, alerts, splash, new field guide.
- 1.0.4 (cairn-alpha5): Chat tab and cairn say / dm / chans commands.
- 1.0.3 (cairn-alpha4): splash stays 5 seconds, MeshCore line removed from the splash.
- 1.0.2 (cairn-alpha3): OLED emblem splash and CAIRN STATION screen with check-in button.
- 1.0.1 (cairn-alpha2): 8-character station ID from the radio's public key; version set to 1.0.1; dashboard reads 5- and 8-character check-ins.
- 0.1.0-alpha (cairn-alpha1): first build, 5-character random ID.

Before going in the field, run through `CAIRN-TEST-CHECKLIST.txt`.
