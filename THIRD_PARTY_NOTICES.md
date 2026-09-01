# Third-party notices

This project links to or builds with the following upstream projects. Their
own license files remain authoritative.

Firmware portions were derived from the MIT-licensed
[`Corvelis/stackchan-pet-fw`](https://github.com/Corvelis/stackchan-pet-fw)
project, copyright 2026 あいろぐ. This standalone repository preserves that
notice in `LICENSE`; support requests for this extracted product belong here.

- M5Unified — M5Stack
- M5GFX — M5Stack
- M5PM1 — M5Stack
- M5IOE1 — M5Stack
- M5UnitUnified — M5Stack
- M5Unit-NFC — M5Stack
- ArduinoJson — Benoit Blanchon
- Flutter and Dart — Google and contributors
- `crop_your_image`, `flutter_image_compress`, `image`, `image_picker`,
  `path`, and `path_provider` Flutter packages

`firmware/scripts/patch_m5unit_nfc.py` carries a narrowly scoped build-time
compatibility patch for M5Unit-NFC 0.1.0. See the script and protocol notes for
the technical reason.

The distributor must confirm that `firmware/assets/default.jpg` is licensed
for redistribution before publishing a release.
