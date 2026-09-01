# Paper Mono NFC Image Transfer

[日本語](README.md) | English

Send an image from Android or iPhone to an M5Stack Paper Mono over NFC, then
show it in an offline e-paper dashboard with a clock, calendar, step counter,
configurable daily goal, and 30-day history.

This is an unofficial, community-maintained project for **M5Stack Paper Mono
C153**. Paper Mono Lite does not contain the NFC hardware required by this
project.

The Paper Mono-specific code was extracted from
[`Corvelis/stackchan-pet-fw`](https://github.com/Corvelis/stackchan-pet-fw) so
this focused product can evolve without adding maintenance burden to the
shared multi-device firmware.

## What is included

- `firmware/`: standalone PlatformIO firmware for Paper Mono C153
- `mobile/`: one Flutter app with Android `NfcA` and iPhone Core NFC transports
- `protocol/`: the versioned wire contract and shared test vectors
- `docs/`: product behavior, architecture, flashing, distribution, and test notes

No Wi-Fi, Bluetooth, cloud account, analytics, or OTA update is required.

## Device experience

The dashboard preserves the existing Paper Mono visual design:

- embedded default image or the last committed NFC image
- large local clock and date
- seven-day strip and full month calendar
- today's steps, configurable goal, and segmented goal counter
- 30 days of on-device step history
- front-light-off low-power lock that keeps RTC and step counting active

Hold `BtnA` for about 0.7 seconds to open the six-card menu:

```text
RECEIVE IMAGE    SYNC CLOCK
STEP GOAL        STEP HISTORY
RESET IMAGE      BACK
```

Press `BtnB` on the dashboard to enter or leave low-power lock. The power key
remains reserved for the device power controller.

## Image and time transfer

Protocol v1 sends images from the phone to Paper Mono. The reverse direction
contains acknowledgements, progress, and errors—not image downloads.

- dashboard image: 386 x 386 pixels
- JPEG: baseline, three components, metadata stripped
- maximum encoded image: 256 KiB
- update: alternating LittleFS slots, committed atomically
- time: phone UTC time and UTC offset are written to the RX8130CE RTC

An interrupted or invalid transfer leaves the previous image intact. `RESET
IMAGE` deletes both received-image slots and immediately falls back to the
default image compiled into the firmware.

## Installation

[GitHub Releases](https://github.com/Corvelis/paper-mono-nfc-image-transfer/releases/latest)
provide a Paper Mono `full.bin` flashable at 0x0000 with the default image
included, a data-preserving `app.bin` update, a signed Android APK, a Google
Play AAB, and SHA-256 values for each version.

- Paper Mono and Android: [release binary installation](docs/install_binary.en.md)
- iPhone: an official TestFlight/App Store link will be added when published;
  until then, build for a physical device with Xcode
- Maintainers: [signing and GitHub Release procedure (Japanese)](docs/releasing.ja.md)

The Android APK attached to normal CI runs is a debug artifact. Use the signed
`android.apk` from GitHub Releases for general installation or redistribution.

## Build

Firmware:

```sh
cd firmware
pio run -e paper-mono
```

Mobile app:

```sh
cd mobile
flutter pub get
flutter test
flutter run
```

The iPhone target requires a physical NFC-capable iPhone, the NFC Tag Reading
capability, and a developer team selected locally in Xcode. No signing identity
is committed to this repository.

## Default image

Replace `firmware/assets/default.jpg` and build again. The PlatformIO pre-build
step validates the JPEG and embeds it into the application binary, so a
separate filesystem upload is not needed for first boot.

The bundled default artwork is also licensed under the MIT License. See
[`firmware/assets/LICENSE.md`](firmware/assets/LICENSE.md).

## License

Original software and documentation in this repository are available under
the MIT License, including the bundled default artwork. Dependencies retain
their own licenses—see [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) and
the [distribution checklist](docs/distribution_checklist.ja.md).

See the [build and flashing guide](docs/building.ja.md),
[product specification](docs/product_spec.ja.md), and
[architecture notes](docs/architecture.md) before changing behavior shared by
the firmware and phone app. Please keep issues and pull requests in this
standalone repository; see [`CONTRIBUTING.md`](CONTRIBUTING.md).
