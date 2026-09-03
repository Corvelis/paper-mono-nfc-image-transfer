# Documentation

[日本語](README.md) | English

The Japanese root README is the default entry point. This index groups the
remaining documents by task.

## For users

| Document | Purpose |
| --- | --- |
| [Installing release binaries](install_binary.en.md) | Install firmware and apps from a GitHub Release |
| [Operation guide](usage.en.md) | Buttons, image transfer, clock sync, Image Library, steps, and low power |
| [Demo video](media/paper-mono-nfc-demo.mp4) | Watch NFC image transfer and Image Library selection |

Japanese versions are available for [installation](install_binary.ja.md) and
[operation](usage.ja.md).

## For developers

| Document | Purpose |
| --- | --- |
| [Build and flashing guide (Japanese)](building.ja.md) | PlatformIO, Flutter, Android, and iPhone development builds |
| [Product specification (Japanese)](product_spec.ja.md) | Scope, UI, image, time, step, and low-power requirements |
| [Architecture](architecture.md) | Responsibilities across app, NFC, storage, and display |
| [Protocol v1](../protocol/protocol_v1.md) | Authoritative NFC wire contract |

A protocol change must update `protocol/test_vectors.json`, firmware, Android,
iPhone, and their tests together. A UI or storage behavior change must also
update the product specification and both operation guides.

## For maintainers and distributors

| Document | Purpose |
| --- | --- |
| [Release procedure (Japanese)](releasing.ja.md) | Signing, tags, GitHub Releases, and local packaging |
| [Distribution license checklist (Japanese)](distribution_checklist.ja.md) | Checks for source, BINs, apps, and default artwork |

See the root [`LICENSE`](../LICENSE) and
[`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md) for licensing details.
