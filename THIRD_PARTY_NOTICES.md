# Third-party notices

This file records the dependency audit performed against the versions locked
by this repository on 2026-09-01. It is a summary, not a replacement for the
complete license texts shipped by each dependency. Upstream license files are
authoritative.

All dependencies listed below permit commercial use. Their attribution,
notice, source-availability, and redistribution conditions still apply.

## Project lineage

Firmware portions were derived from the MIT-licensed
[`Corvelis/stackchan-pet-fw`](https://github.com/Corvelis/stackchan-pet-fw),
copyright 2026 あいろぐ. This standalone repository preserves that copyright
notice in `LICENSE`; support requests for this extracted product belong here.

## Firmware runtime dependencies

| Component | Locked version | License | Upstream |
| --- | --- | --- | --- |
| M5Unified | 0.2.20 | MIT | [m5stack/M5Unified](https://github.com/m5stack/M5Unified) |
| M5GFX | 0.2.27 | MIT | [m5stack/M5GFX](https://github.com/m5stack/M5GFX) |
| M5PM1 | `be9a5456` | MIT | [m5stack/M5PM1](https://github.com/m5stack/M5PM1) |
| M5IOE1 | `846eec7d` | MIT | [m5stack/M5IOE1](https://github.com/m5stack/M5IOE1) |
| M5UnitUnified | 0.5.0 | MIT | [m5stack/M5UnitUnified](https://github.com/m5stack/M5UnitUnified) |
| M5Unit-NFC | 0.1.0 | MIT | [m5stack/M5Unit-NFC](https://github.com/m5stack/M5Unit-NFC) |
| M5Utility | 0.2.0, transitive | MIT | [m5stack/M5Utility](https://github.com/m5stack/M5Utility) |
| M5HAL | 0.1.2, transitive | MIT | [m5stack/M5HAL](https://github.com/m5stack/M5HAL) |
| ArduinoJson | 7.4.3 | MIT | [bblanchon/ArduinoJson](https://github.com/bblanchon/ArduinoJson) |
| Arduino-ESP32 | 2.0.17 (`3.20017.241212`) | LGPL-2.1-or-later | [espressif/arduino-esp32](https://github.com/espressif/arduino-esp32/tree/2.0.17) |

`firmware/scripts/patch_m5unit_nfc.py` applies project-specific modifications
to the downloaded M5Unit-NFC 0.1.0 sources at build time. A resulting firmware
binary is therefore not an unmodified upstream build. The M5Unit-NFC MIT
copyright and permission notice must remain with redistributed source or
binaries.

Arduino-ESP32 is the main copyleft-licensed runtime component identified in
this audit. Commercial use is allowed under the LGPL, but firmware-binary
distributors must satisfy the LGPL's notice, corresponding-source or relinking,
modification, and reverse-engineering conditions. The framework package also
contains ESP-IDF and other components with their own permissive and third-party
notices; the complete corresponding framework sources and notices must be
preserved. See the distribution checklist before publishing a `.bin` file or
shipping hardware.

## Mobile runtime dependencies

The Flutter application is built with Flutter/Dart (BSD-3-Clause) and the
following direct packages locked in `mobile/pubspec.lock`:

| Package | Locked version | License |
| --- | --- | --- |
| `cupertino_icons` | 1.0.8 | MIT |
| `crop_your_image` | 2.0.0 | Apache-2.0 |
| `flutter_image_compress` | 2.5.1 | MIT |
| `image` | 4.9.2 | MIT |
| `image_picker` | 1.2.1 | Apache-2.0 |
| `path` | 1.9.1 | BSD-3-Clause |
| `path_provider` | 2.1.5 | BSD-3-Clause |

The transitive Dart/Flutter packages in `mobile/pubspec.lock` were also
checked from their package-root `LICENSE` files. They use MIT,
BSD-3-Clause, or Apache-2.0. The `mime` package additionally carries its own
`third_party/httpd/LICENSE` notice.

Native dependencies currently locked by the mobile projects include:

- AndroidX libraries and Apache Commons IO — Apache-2.0
- SDWebImage 5.21.7 and SDWebImageWebPCoder 0.15.0 — MIT
- libwebp 1.6.0 — BSD-style license
- Material Icons — Apache-2.0

Flutter collects package-root license files into `NOTICES.Z` in the app asset
bundle. The app exposes those collected notices from the information icon in
its app bar. Native Gradle/CocoaPods notices must also be checked in the final
signed APK/AAB/IPA release process because they are resolved per target.

## Build tools and platform SDKs

- PlatformIO Core and PlatformIO's Espressif32 platform 6.12.0 are
  Apache-2.0. They are build tools and are not linked into the firmware as a
  whole.
- Espressif esptool is GPL-2.0-or-later and runs only as an external uploader.
  Its GPL does not relicense the generated firmware, but redistributing
  esptool itself requires satisfying the GPL.
- The Xtensa GCC toolchain is GPL-licensed with applicable runtime-library
  exceptions. It is a compiler tool, not project source; its own terms apply
  if the toolchain is redistributed.
- Gradle 8.12, Android Gradle Plugin 8.7.3, and Kotlin tooling 2.1.0 are
  Apache-2.0. CocoaPods 1.16.2 is MIT. Python and other tool components retain
  their own licenses.
- Normal use of these tools to build the project does not relicense the
  project output. Redistributing the tools themselves requires preserving
  their licenses and satisfying any source-distribution conditions.
- Android SDK use is governed by the Android SDK License Agreement. Google Play
  distribution is governed separately by the applicable developer agreement.
- Xcode, Apple SDKs, Core NFC, code signing, and App Store distribution are
  governed by Apple's developer agreements. They are not open-source
  dependencies bundled from this repository.

## Project artwork

`firmware/assets/default.jpg` is original project artwork licensed under
the MIT License, including commercial use. Its exact scope and copyright are in
`firmware/assets/LICENSE.md`.
