# Contributing

This repository is intentionally independent from the shared multi-device
firmware it was derived from. Please report bugs and propose changes here,
rather than opening issues against the upstream repository.

Before submitting a change:

1. Keep the firmware target limited to M5Stack Paper Mono C153.
2. Do not add Wi-Fi, BLE, cloud, analytics, or account dependencies.
3. Update `protocol/protocol_v1.md` and its vectors when the wire format changes.
4. Run the firmware build, Flutter analysis/tests, and the relevant native build.
5. State whether a change was verified on physical Paper Mono, Android, or iPhone hardware.

Never commit signing identities, provisioning profiles, API keys, or personal
Android/iOS build configuration.
