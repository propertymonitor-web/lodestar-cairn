# CAIRN architecture (Ver. 1.4.1)

CAIRN is a crisis-use field kit for MeshCore radios. It is firmware for a Heltec V4 R2 *companion* radio, a tablet app, and a dashboard. **Nothing here has run on real hardware yet** (see `SECURITY.md` and the status section in `README.md`). This file describes how the pieces fit, so a reviewer can find their way around.

## The pieces

```
  LoRa radios (other stations, repeaters)
          |  MeshCore radio protocol
  +-------v---------------------------+        +----------------------------+
  | Heltec V4 R2 running MeshCore +   |  USB   | CAIRN Station (Android)    |
  | CAIRN patch                       +--------+  - dashboard in a WebView  |
  |  - journal of everything heard    |  or    |  - Bluetooth to MeshCore   |
  |  - check-in, help beacon, units   | Bluetooth  app (chat)              |
  |  - legal-power logic              |        |  - optional internet tools |
  +-----------------------------------+        +----------------------------+
```

- **Firmware** is MeshCore's companion radio with CAIRN patched in. MeshCore is downloaded at build time; this repository holds only the patch and CAIRN's own code.
- **Dashboard** (`dashboard/`, built into `docs/`) is one HTML page that reads the radio's journal and status and sends commands. It also runs from GitHub Pages as an installable page.
- **Android app** (`android/`) wraps the dashboard, talks to the radio over USB, and carries CAIRN text over Bluetooth.

## Firmware (`cairn/`)

| File | What it does |
|---|---|
| `cairn/Cairn.h` | All the logic, with no hardware calls so it can be tested on a PC. Commands, settings, journal, channels, units, power limits. |
| `cairn/CairnOut.h` | The Bluetooth text link: a framed byte stream (frame type `0xC8`) and a bounded byte ring. |
| `cairn/ui/` | The 128x64 OLED screens (station, help, splash). |
| `tools/apply_cairn.py` | Patches a MeshCore checkout: hooks CAIRN into received packets, the command line, the screen and the loop. Fails loudly if upstream changed the code it expects. |
| `tools/persist_ble_pin.py` | Test-build convenience: makes the Bluetooth pairing PIN the same at every restart. Intentionally weaker than a random PIN; to be removed from non-test releases (see `SECURITY.md`). |
| `nova/`, `lodestar/` | Shared code CAIRN is built on (NOVA companion extensions, LODESTAR scoring header). |

### Data it keeps

- **Journal.** Every message, advert and own check-in becomes one JSON line (at most 640 bytes). Lines pass through a 24-line RAM ring (`Journal`) to two writers: a file in flash (rotated at about 192 KB) and the USB/Bluetooth stream. Each reader has its own cursor, a slow reader loses old lines and is told how many (`lost`), and a line that does not fit is refused, never cut. Text from other people is escaped, control characters become spaces, and anything that is not valid UTF-8 becomes `?`, so every line is strict JSON.
- **Settings.** One line `CAIRN2 ...` in `/cairn.cfg` (mode, power preference, antenna gain, auto-return, beacon interval, main channel, help, battery options). Loading rejects anything out of range and leaves the old values alone.
- **Units.** Up to 48 remote companions/repeaters, each bound to a radio ID, with a CRU number and call name.
- **Station ID.** `LC-` plus 8 characters taken from the last 5 bytes of the radio's public key, which MeshCore creates once. It survives reboots and firmware updates and changes only if the radio's identity is erased.

### Power and the legal limit

For the US 915 MHz band (47 CFR 15.247) the code keeps the radio's setting at or under 30 dBm of effective output after the antenna gain. The limit falls by 1 dB for each dBi of antenna above 6. The radio's real output is estimated as setting + 12 dB, capped at 28 dBm, with 1 dB of margin. Outside 902 to 928 MHz the code refuses to choose a power (`-100`) and leaves the radio alone. **This is a calculation from the manufacturer's numbers. It has not been measured on a real radio.** The power calculator in `Cairn.h` (`namespace power`) is the one place this logic lives.

### Modes

- **Standard**: normal power, check-ins on request.
- **Disaster**: highest legal power, for emergencies. A help beacon repeats on a timer and the auto-return timer can bring the radio back to Standard.
- **Saver / battery saving**: caps power at a lower setting, and switches on automatically at low battery (on at 25 percent, off at 35 percent, not on external power).

## Tablet app (`android/`)

Kotlin, minimum code around a WebView. `UsbLink` and `BleLink` carry bytes; `Tunnel` frames CAIRN text; `Bridge` is the only JavaScript to native interface; `Fetcher` handles the optional internet features (weather alerts, earthquakes, map download, updates). Cleartext traffic and backups are off. The app does not work out anything about the radio itself; it relays.

## Dashboard (`dashboard/`)

`station.js` and `cairn.html` render the status, chat, units, map, alerts and the power calculator. `tools/build_dashboard.py` assembles one file and copies it into `docs/` and into the app, and CI fails if the copies drift from the source.

## Build and checks (`.github/workflows/build.yml`)

On every push CI: checks the dashboard copies and version labels agree; runs the NOVA and CAIRN unit tests; runs the **edge-case suite** (random garbage commands, damaged settings and unit files, hostile message text, journal overload) with AddressSanitizer, UndefinedBehaviorSanitizer and float-cast checking on; checks every emitted journal line is strict JSON; tests the dashboard in a real browser at tablet and phone sizes; patches a fresh MeshCore and draws every OLED screen on a pretend display; builds and checks the Android app; and builds the firmware for the Heltec V4 R2.

## What is not covered

Tests run on a PC. They prove the logic handles good and bad input. They do not prove behavior on the board: radio output power, battery readings, the Bluetooth link under real conditions, long-term flash wear, or range. See `CAIRN-TEST-CHECKLIST.txt` for what to check by hand.
