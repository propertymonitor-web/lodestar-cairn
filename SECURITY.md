# Security notes for CAIRN (Ver. 1.4.1)

This is a Beta that **has not run on real hardware**. It is meant for tests and drills, not yet for relying on in an emergency. This page lists what we know can go wrong. If you find something we missed, please tell us (below).

## Reporting a problem

Use GitHub's private vulnerability reporting on this repository (Security tab, "Report a vulnerability"). Please do not post details publicly until we have had a chance to fix it. This is a small volunteer project, so we cannot promise a response time.

## What CAIRN is built to resist

- **Hostile text from the mesh.** Anyone on the same channel can send anything. Message text, names and channel names are escaped before they reach the journal or the dashboard, control characters are replaced, and bytes that are not valid UTF-8 are replaced with `?`. The dashboard runs received text through an escaping function before putting it in a page. We have spot-checked this but have not audited every one of the roughly 50 places the dashboard builds HTML, so treat it as likely, not proven.
- **Bad commands and bad files.** Commands are length-checked and range-checked, and settings and unit files that are damaged are rejected whole. The edge-case test suite throws about 120,000 random commands and thousands of damaged files at these paths with memory and undefined-behavior checkers on.
- **Overload.** The journal is a fixed-size ring. A slow reader loses old lines and is told; the radio does not stall.
- **Transmitting over the legal limit.** The power logic refuses to pick a setting outside 902 to 928 MHz and lowers power as antenna gain rises (see `ARCHITECTURE.md`).

## Known weaknesses (please read)

1. **Testing feature: the Bluetooth pairing PIN is predictable on purpose (for now).** Stock MeshCore picks a random PIN each start, which breaks the phone's saved pairing after every restart and makes testing slow. For test builds, `tools/persist_ble_pin.py` replaces that with a number worked out from the chip's own ID, so testers can pair once and keep the pairing. This is a **known and intentional testing convenience, and it is a real weakness in the builds that include it**: the method is open source and the chip's address is announced over Bluetooth, so a person within range who reads this code can work out the PIN, and could then read the radio's traffic or send as the station. **Plan:** releases that are not meant for testing will not use this patch; they will use a random secret created once at first boot and kept on the radio (or stock MeshCore behavior). That change has **not been made or tested yet**, and it will be listed in the release notes when it is. Until then, do not rely on Bluetooth pairing for anything sensitive, and use the USB link or `set pin` if your build supports it.
2. **Mesh messages are not private from other channel members.** Channel and hashtag-channel keys are shared secrets. Anyone with the key can read and send. Hashtag channels derive their key from the name, so anyone who guesses the name can join.
3. **The station ID and check-ins are not authenticated beyond MeshCore.** A check-in, a help message and a unit registration are only as trustworthy as the channel they arrive on.
4. **The Android app is signed with a key that is in this repository** (`android/alpha.keystore`, with its password in `build.gradle.kts`). The signature therefore proves nothing about who made the file. Install it only from this repository's releases, check the checksum, and treat it as test software. Replacing this with a private signing key is on the list.
5. **Updates are not verified.** The dashboard's update check and the app's update download trust what GitHub returns. Release files are not signed.
6. **The build downloads other code.** MeshCore and its libraries are fetched when the firmware is built and are not pinned to a verified hash, so a change upstream can change what you build. `tools/apply_cairn.py` fails if the code it patches has changed, which catches some of this but is not a security control.
7. **Internet features.** Weather, earthquake, map and update features call outside services over HTTPS. They send the tablet's IP address and, for weather and maps, an approximate location. They are off when the tablet has no internet.
8. **The radio is not hardened as a device.** Anyone with physical access can read the journal, the settings and the keys, or erase them. Nothing is encrypted at rest.
9. **Not tested on the target hardware.** Radio power, battery behavior, the Bluetooth link and the flash filesystem under long use are untested.

## Radio regulations

Operating a radio is the operator's responsibility. CAIRN's power limit is a calculation for the US 902 to 928 MHz band using the manufacturer's figures and the antenna gain you enter. It has not been measured, and it does not check your country, license or local rules.

## How this code was made

Most of the code and documentation in this repository was written with an AI assistant (Claude, by Anthropic), directed and reviewed by the project's authors. The commit history records the assistant as co-author. Passing tests do not make code correct; independent review is welcome.
