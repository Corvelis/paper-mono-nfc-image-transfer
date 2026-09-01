# Architecture

```text
Flutter UI
  | Method/Event channels
  +-- Android NfcA transport
  +-- iPhone Core NFC transport
             |
             | Paper Mono NFC Protocol v1
             v
PaperMonoNfcController
  +-- PSRAM receive/verify buffer
  +-- LittleFS alternating image slots
  +-- RX8130CE time update
             |
             v
AppController
  +-- DashboardView
  +-- StepCounterController / Preferences
  +-- RTC clock / UTC offset
  +-- Active and LowPowerLocked power states
```

The root `protocol/` directory is authoritative. Native platform copies must
be tested against its vectors. NFC callbacks only parse, copy, and answer.
JPEG verification, storage, and e-paper refresh run from the firmware loop.

The embedded default image is independent from LittleFS. A fresh or formatted
filesystem therefore still produces a useful first boot.
