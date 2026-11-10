# Third-party notices

LODESTAR CAIRN is licensed under the Apache License 2.0 (see `LICENSE`). It builds on, bundles or loads the work of others, which stays under its own license. This list was put together by hand from the repository and has not been checked by a lawyer. If you find a mistake, please open an issue.

## Built on

**MeshCore** (https://github.com/meshcore-dev/MeshCore), MIT License. CAIRN does not include MeshCore's source in this repository: `tools/apply_cairn.py` patches a copy that is downloaded when you build. Firmware you build contains MeshCore and its own dependencies (for example the RadioLib radio library and the board support packages for each chip), each under its own license. Their license files come with their source; if you distribute a built firmware, keep their notices with it.

## Included in this repository

**Leaflet 1.9.4**, BSD 2-Clause License. Files: `dashboard/libs.html` (script, included unchanged) and `dashboard/leaflet.css`.

> Copyright (c) 2010-2023, Vladimir Agafonkin
> Copyright (c) 2010-2011, CloudMade
> All rights reserved.
>
> Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
>
> 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
>
> 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
>
> THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## Used by the CAIRN Station Android app (`android/`)

These are downloaded by the Android build, not stored in this repository.

- **usb-serial-for-android** 3.8.1 (mik3y and contributors), MIT License.
- **AndroidX core-ktx 1.13.1 and androidx.webkit 1.11.0** (The Android Open Source Project), Apache License 2.0.
- **JUnit 4.13.2** (tests only), Eclipse Public License 1.0.

## Loaded from the internet when you use the dashboard

- **esptool-js** (Espressif), loaded from the jsDelivr network to flash a radio from the browser. It is not stored here. Its license is stated in its own repository.
- **OpenStreetMap** map data and tiles are (c) OpenStreetMap contributors, available under the Open Database License (https://www.openstreetmap.org/copyright). If you use the map download feature, follow the OpenStreetMap tile usage policy.
- **Open-Meteo, USGS and Google Gemini** are online services the dashboard can call. Each has its own terms of use, which apply to your use of them.

## Names

"CAIRN", "LODESTAR" and the emblem are names and marks of their owners and are not licensed by the Apache License. The Apache License does not grant permission to use them to suggest that your product is ours.
