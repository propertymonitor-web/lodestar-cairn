# CAIRN Station - the Android tablet app

Engineered by: Aries Thorne & Todd Steinhauer for Wolf Reactive Systems (WRS)
Special thanks to the NTXMESH community, Meshcore Community, and Pat Steinhauer-Scates for their guidance and support.

**BETA. This app has been compiled and unit-tested by the GitHub build and its screens have been tested in a desktop browser with a pretend tablet. It has NOT yet been run on a real tablet, with a real radio or a real repeater.** Treat the first run as a test and report what you see.

## What it is

The CAIRN dashboard, stored inside an Android app, so the tablet starts straight into the station:

1. **Splash screen** (scales to the screen).
2. **Wi-Fi** - the only step that can be skipped.
3. **Repeater** - USB cable (serial, 115200 baud). Needed.
4. **Companion radio** - Bluetooth. Needed.
5. **Dashboard** - with **Files** (logs and files CAIRN makes), **Online** (Wi-Fi extras) and **Ask Gemini** (turned off when there is no internet).

If the repeater or the radio drops out later, the steps come back until both are connected again.

## What the app adds to the browser dashboard

| Need | How |
|---|---|
| Bluetooth to the companion radio | Native Android Bluetooth LE (Nordic UART service). CAIRN **firmware 1.2.0 or newer** carries its text commands inside one extra Bluetooth frame type (0xC8). Older firmware will connect but never answer. |
| USB serial to the repeater | `usb-serial-for-android`. Shows the repeater's own text, asks it for status every 30 s, logs it. |
| Logs and files | Day files in the app's private storage: `journal-DATE.log` (what the radio heard), `repeater-DATE.log`. Reports and saved pages. Send by email or copy to Download/CAIRN. |
| Wi-Fi extras | US National Weather Service forecast and alerts, USGS earthquakes (last 7 days, 500 km), update check against this repo's releases, offline-map downloads to the tablet. Only a short list of hosts is allowed (api.weather.gov, earthquake.usgs.gov, api.github.com, github.com, githubusercontent.com), https only. Last answers are kept and shown with their age when offline. |
| Gemini | Opens the Gemini app (`com.google.android.apps.bard`), or its web page if the app is missing. Disabled when Android reports no working internet. |

## Install on the tablet

1. Download `cairn-station-1.3.0.apk` from the Releases page (on the tablet, with Wi-Fi).
2. Open it from Files/Downloads. Android asks to allow "install unknown apps" for that app: allow it once.
3. Open **CAIRN Station**. Allow Bluetooth when asked.
4. To make it the tablet's start-up screen: Android **Settings > Apps > Default apps > Home app** (the wording differs by maker) and choose **CAIRN Station**. The Android navigation bar stays available, so Gemini and Settings can still be reached; Home returns to CAIRN.
5. Flash CAIRN **1.3.0** firmware to the companion radio (see the setup guide). The repeater keeps its normal repeater firmware.

First Bluetooth connection: Android asks for the PIN shown on the radio's own screen.

## Build it yourself

GitHub Actions builds it (job "Build the Android tablet app"). Locally: `python3 tools/build_dashboard.py`, copy `docs/index.html` to `android/app/src/main/assets/station.html`, then `gradle assembleRelease` in `android/` (Android SDK 35, JDK 17, Gradle 8.9).

## Honest limits

- **Signing key.** `alpha.keystore` is committed to the repo on purpose so that every build installs over the last one. Its password is in `app/build.gradle.kts`. It is **not a secret**, so it proves nothing about who built an app. Replace it with a private key before the app is sold or given to anyone who needs to trust where it came from.
- **No lock-down.** The app is a Home app, not a locked kiosk. A device-owner lock (blocking Android Settings and other apps) needs a factory-fresh tablet set up from a computer and is not included. Android's own "App pinning" would also block Gemini, so it is not used.
- **Does not run in the background.** If Android closes the app while another app is in front for a long time, the links drop and the steps come back when you return. A background service is a possible later addition.
- **One Bluetooth client.** The radio talks to one app at a time: close the MeshCore phone app first.
- **Weather and quakes** cover the United States (NWS) and worldwide (USGS) respectively; the weather service answers "no forecast" outside US territory.
- **Not tested on real hardware yet:** USB permission flow on Android 16, BLE pairing and the 0xC8 text link, map range serving, the file picker, the install-update prompt. The tablet in question is described only as a generic "Android 16 10-inch 1280x800" model; its exact maker and chip are unknown, and the app does not depend on them.
- A custom operating system is not part of this and is not recommended: an unknown chip, brick risk, and loss of the DRM certification the tablet is sold with.
