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
  +-- LittleFS 17 received images + one atomic staging slot
  +-- Dual-generation NVS image catalog
  +-- RX8130CE time update
             |
             v
AppController
  +-- DashboardView
  |     +-- BatteryLevelEstimator
  +-- StepCounterController / Preferences
  +-- RTC clock / UTC offset
  +-- Active and LowPowerLocked power states
```

The root `protocol/` directory is authoritative. Native platform copies must
be tested against its vectors. NFC callbacks only parse, copy, and answer.
JPEG verification, storage, and e-paper refresh run from the firmware loop.

The Flutter layer owns layout selection, fixed-aspect cropping, monochrome JPEG
generation, preview, and one resumable pending transfer. Android and iOS only
provide their platform NFC transports. Clock sync and image transfer are
separate sessions; the image path never sends `SET_TIME`.

During Android transfer, a Paper Mono RF reset can cause the NFC service to
replace the active `Tag` object. The Android transport queues that replacement,
continues with the newest tag, and confirms a matching persisted transfer via
`BEGIN`/`COMPLETED`. An `out of date` tag is therefore recoverable and is not
reported as a completed-image failure.

Each committed LittleFS image has mode, dimensions, byte count, CRC, sequence,
and slot metadata in the dual-generation NVS catalog. The catalog contains no
user-visible image name. Entries are ordered newest first, the active entry is
tracked separately, and committing image 18 evicts the oldest of the 17
received entries. The compiled default image is a protected virtual entry and
does not consume a LittleFS slot.

The embedded default image is independent from LittleFS. A fresh or formatted
filesystem therefore still produces a useful first boot.

Battery percentage is derived from the Paper Mono PMIC voltage using a
3.3–4.2 V linear range. `BatteryLevelEstimator` applies a small deadband and a
one-quarter low-pass update, and retains the last valid sample across a
transient PMIC read failure. The step detector keeps its cadence rejection and
applies the calibrated 9/7 scale only after a gait cycle has been accepted.
