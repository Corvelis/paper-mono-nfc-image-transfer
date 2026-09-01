# Installing release binaries

This guide is for people installing the M5Stack Paper Mono C153 firmware and
phone app from GitHub Releases. For source builds and development, see the
[build and flashing guide](building.ja.md).

## 1. Choose a release

Download files with the same version number from the
[latest GitHub Release](https://github.com/Corvelis/paper-mono-nfc-image-transfer/releases/latest).

| File | Purpose |
| --- | --- |
| `paper-mono-nfc-image-transfer-vX.Y.Z-full.bin` | First/clean installation image flashed at 0x0000 |
| `paper-mono-nfc-image-transfer-vX.Y.Z-app.bin` | Data-preserving compatible update flashed at 0x10000 |
| `paper-mono-image-sender-vX.Y.Z-android.apk` | Signed Android app for direct installation |
| `paper-mono-image-sender-vX.Y.Z-android.aab` | Google Play upload; not directly installable |
| `*-android-signing-certificate.txt` | Public Android signing-certificate fingerprints |
| `*-firmware-components.zip` | Split images for recovery and development |
| `*-notices.zip` | Project/collected dependency notices and installation documents |
| `release-manifest.json` | Target, commit, protocol, and build metadata |
| `SHA256SUMS` | SHA-256 download verification values |

For the first Paper Mono installation, use `full.bin`. For a compatible update
that preserves existing data, use `app.bin`. For direct Android installation,
use the `.apk`. The default MIT-licensed artwork is embedded in both firmware
images, so no separate filesystem image is required. Follow the Release Notes
instead if they require a partition migration or clean installation.

## 2. Verify the download

Calculate the file hash and compare it with the matching line in `SHA256SUMS`.

macOS:

```sh
shasum -a 256 paper-mono-nfc-image-transfer-vX.Y.Z-full.bin
```

Linux:

```sh
sha256sum paper-mono-nfc-image-transfer-vX.Y.Z-full.bin
```

Windows PowerShell:

```powershell
Get-FileHash .\paper-mono-nfc-image-transfer-vX.Y.Z-full.bin -Algorithm SHA256
```

Do not use the file if the value differs; download it again.

## 3. Flash Paper Mono

The firmware is only for the NFC-equipped **M5Stack Paper Mono C153**. Do not
flash it to Paper Mono Lite or another ESP32 product. Use a data-capable USB
cable, install Python 3, then install the flashing tool:

```sh
python3 -m pip install "esptool==4.9.0"
```

1. Connect Paper Mono to the computer over USB.
2. Hold the side reset button for about two seconds. Release it when the side
   LED blinks to enter Download Mode.
3. Find the port. Common forms are `/dev/cu.usbmodem...` on macOS,
   `/dev/ttyACM...` on Linux, and `COM...` on Windows.
4. Choose either a first/clean installation or a data-preserving update.

### First or clean installation

Replace `<PORT>` and the filename, then flash `full.bin` at 0x0000:

```sh
python3 -m esptool --chip esp32s3 --port <PORT> --baud 460800 \
  write_flash 0x0000 paper-mono-nfc-image-transfer-vX.Y.Z-full.bin
```

Because a merged `full.bin` also writes the gaps between its components, it
resets step history, the step goal, time-zone setting, and active-image metadata
stored in NVS. To return all storage to a completely clean state, erase first;
this also removes received images from LittleFS:

```sh
python3 -m esptool --chip esp32s3 --port <PORT> erase_flash
```

### Data-preserving compatible update

If this project's firmware is already running and the Release Notes do not
mention a partition change, flash only `app.bin` at 0x10000:

```sh
python3 -m esptool --chip esp32s3 --port <PORT> --baud 460800 \
  write_flash 0x10000 paper-mono-nfc-image-transfer-vX.Y.Z-app.bin
```

This does not write NVS or LittleFS, preserving the received image, step
history, goal, and time-zone setting. Do not use it when migrating from another
project, when a Release changes partitions, or for recovery from an unbootable
installation; use `full.bin` instead.

Press reset briefly after flashing. The installation is successful when the
image, clock, calendar, steps, and goal counter appear. See M5Stack's official
[Paper Mono programming guide](https://docs.m5stack.com/en/arduino/papermono/program)
for its Download Mode instructions.

## 4. Install the Android app

You need an NFC-A phone running Android 7.0 (API 24) or later.

1. Download the signed `android.apk` from the same Release to the phone.
2. Temporarily allow “Install unknown apps” for the browser or file manager
   opening the APK.
3. Open the APK and install it.
4. Disable that temporary permission afterward if it is no longer needed.

Only an APK with the same Application ID and signing key can update an existing
installation. If Android reports a signature conflict, first check where the
installed app and APK came from. The SHA-256 certificate value can also be
compared across each Release's `android-signing-certificate.txt`. Uninstalling
also removes app-local data. An
`.aab` is a Google Play upload artifact and cannot be installed directly.

## 5. Install the iPhone app

iOS apps require Apple-issued signing appropriate to the destination device,
so this project does not publish an unusable unsigned IPA in GitHub Releases.
General distribution uses TestFlight or the App Store; their official link will
be added to the Release Notes and README when available. Until then, use macOS,
Xcode, and your Apple Developer configuration to perform a
[physical-device source build](building.ja.md#iphone).

See Apple's
[Xcode distribution guide](https://developer.apple.com/documentation/xcode/distributing-your-app-for-beta-testing-and-releases)
for the supported distribution paths.

## 6. First connection

1. Hold `BtnA` on Paper Mono for about 0.7 seconds.
2. Select `SYNC CLOCK` first and send UTC time plus the UTC offset from the app.
3. Open the menu again, select `RECEIVE IMAGE`, choose an image in the app, and
   send it.
4. Keep the phone's NFC antenna against Paper Mono until completion is shown.

Use `RESET IMAGE` to restore the embedded image; step history, goal, and clock
settings remain intact. Press `BtnB` on the dashboard to enter the front-light-
off low-power lock, and press it again to return.

## Troubleshooting

- No serial port: use a data-capable USB cable and enter Download Mode again.
- Flashing waits for a connection: close serial monitors and recheck the port.
- Android APK will not update: verify the origin and signing identity of both
  the installed app and Release APK.
- NFC does not start: open `SYNC CLOCK` or `RECEIVE IMAGE` on Paper Mono first,
  and enable NFC on the phone.
- Transfer disconnects: find the phone's NFC antenna position and hold it still
  until the transfer completes.
