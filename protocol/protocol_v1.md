# PaperMono NFC image protocol v1

This document is the wire contract shared by the PaperMono firmware and the
Android/iOS image sender. Multi-byte integers are unsigned and little-endian.
Protocol lengths exclude the ISO 14443-A CRC-A bytes. Android adds them in its
NFC stack. For the tested iPhone/Core NFC and PaperMono ST25R3916 combination,
the iOS sender passes protocol bytes to `sendMiFareCommand()` without a
software CRC; the RF layer supplies CRC-A and M5Unit-NFC removes it before
protocol parsing. Appending CRC-A in Swift was observed as an extra two bytes
after M5Unit-NFC's removal and made HELLO fail with `INVALID_LENGTH`.

## Transport limits

| Name | Value |
|---|---:|
| M5Unit-NFC emulation receive buffer | 256 bytes |
| NFC-A FIFO frame, including CRC-A | 255 bytes |
| Protocol command, excluding CRC-A | 253 bytes |
| DATA payload | 1–240 bytes |
| Image | 1–262,144 bytes |
| Incomplete transfer TTL | 120 seconds |

The protocol permits up to 240 payload bytes. Production senders prefer 128
bytes and fall back to 64 after a transport failure; this is more reliable at
the ST25R3916 FIFO boundary. The absolute maximum DATA command is 253 bytes,
or 255 bytes after CRC-A is added.

## Encoding

- Magic: `50 4D` (ASCII `PM`)
- Version: `01`
- Byte order: little-endian
- Transfer ID: non-zero unsigned 32-bit integer
- Image checksum: CRC-32/ISO-HDLC
- RF checksum: CRC-A, not included in protocol lengths

CRC-32/ISO-HDLC uses polynomial `0x04C11DB7` (reflected `0xEDB88320`),
initial value `0xFFFFFFFF`, reflected input/output and final XOR
`0xFFFFFFFF`. The check value for `123456789` is `0xCBF43926`.

## Commands

| Command | Value | Response |
|---|---:|---:|
| HELLO | `01` | `81` |
| BEGIN | `02` | `82` |
| DATA | `03` | `83` |
| STATUS | `04` | `84` |
| COMMIT | `05` | `85` |
| ABORT | `06` | `86` |
| SET_TIME | `07` | `87` |

Every command starts with:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | Magic `50 4D` |
| 2 | 1 | Version `01` |
| 3 | 1 | Command |

### HELLO

The request is the four-byte common header. Its response appends these 11
capability bytes to the common response envelope:

| Extra offset | Size | Field |
|---:|---:|---|
| 0 | 2 | Maximum RF frame bytes, including CRC-A (`255`) |
| 2 | 2 | Maximum protocol command bytes (`253`) |
| 4 | 2 | Maximum DATA payload bytes (`240`) |
| 6 | 4 | Maximum image bytes (`262144`) |
| 10 | 1 | Capabilities; bit 0 is Baseline 3-component JPEG, bit 1 is SET_TIME |

### BEGIN (23 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Common header |
| 4 | 4 | Transfer ID |
| 8 | 1 | Flags; bit 0 is `REPLACE` |
| 9 | 1 | Mode: `01` dashboard |
| 10 | 1 | Format: `01` Baseline 3-component JPEG |
| 11 | 2 | Width |
| 13 | 2 | Height |
| 15 | 4 | Total JPEG bytes |
| 19 | 4 | JPEG CRC-32 |

Dashboard images are exactly 386×386.

Repeating BEGIN with the same ID and identical metadata resumes the transfer.
A conflicting transfer returns `CONFLICT` or `BUSY`; `BEGIN(REPLACE)` may
discard an incomplete transfer.

### DATA (13-byte header plus payload)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Common header |
| 4 | 4 | Transfer ID |
| 8 | 4 | Byte offset |
| 12 | 1 | Payload length (1–240) |
| 13 | 1–240 | Payload |

DATA is idempotent. A chunk at `nextExpectedOffset` is appended. A fully stored
duplicate is ACKed only when its bytes match. A future or partially overlapping
chunk returns `BAD_OFFSET` with the required offset.

### STATUS and ABORT (8 bytes)

The common header is followed by a four-byte transfer ID.

### SET_TIME (15 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Common header |
| 4 | 8 | Unix time in UTC seconds |
| 12 | 2 | Signed UTC offset in minutes |
| 14 | 1 | Flags; must be zero in v1 |

The valid UTC range is 2023-01-01 through the end of 2099 and the offset range
is -840 through +840 minutes. The firmware writes UTC to RX8130CE and stores
the offset separately. Clock synchronization is an explicit, independent flow:
it issues SET_TIME after HELLO and ends after a successful response. Image
transfer never requires SET_TIME and proceeds from HELLO directly to BEGIN, so
image-only v1 receivers remain compatible.

### COMMIT (12 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Common header |
| 4 | 4 | Transfer ID |
| 8 | 4 | Image CRC-32 |

COMMIT returns immediately. CRC/JPEG validation, persistence and display work
run outside the NFC callback. The sender polls STATUS until `STORED`, then
ends its reader session before the blocking e-paper refresh starts.

## Response envelope (13 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | Magic `50 4D` |
| 2 | 1 | Version `01` |
| 3 | 1 | Request command OR `80` |
| 4 | 1 | Status |
| 5 | 4 | Transfer ID, or zero |
| 9 | 4 | Next expected DATA offset |

| Value | Name | Value | Name |
|---:|---|---:|---|
| `00` | OK | `01` | ACCEPTED |
| `02` | BUSY | `03` | CONFLICT |
| `04` | BAD_MAGIC | `05` | UNSUPPORTED_VERSION |
| `06` | UNKNOWN_COMMAND | `07` | INVALID_LENGTH |
| `08` | PAYLOAD_TOO_LARGE | `09` | IMAGE_TOO_LARGE |
| `0A` | BAD_OFFSET | `0B` | DATA_MISMATCH |
| `0C` | CRC_MISMATCH | `0D` | INVALID_JPEG |
| `0E` | UNSUPPORTED_FORMAT | `0F` | NOT_FOUND |
| `10` | INTERNAL_ERROR | `11` | RECEIVING |
| `12` | VERIFYING | `13` | STORED |
| `14` | DISPLAYING | `15` | COMPLETED |

The receiver state sequence is:

```text
RECEIVING -> VERIFYING -> STORED -> DISPLAYING -> COMPLETED
                         \-> CRC_MISMATCH
                         \-> INVALID_JPEG
```

`STORED` is the durable success boundary and terminates the NFC session.
`COMPLETED` may be returned when already available and additionally confirms
that the e-paper refresh finished; the sender must not keep RF open waiting for it.

## JPEG v1

- 8-bit Baseline DCT (`SOF0`)
- Exactly three components
- Gray pixels represented by equal R/G/B values
- No progressive JPEG, CMYK, EXIF, ICC or comments
- Dimensions exactly 386×386
- Maximum 262,144 bytes

The firmware quantizes the decoded JPEG for the monochrome panel. The sender
previews the final encoded JPEG rather than the pre-encode source.

## Timing and recovery

- Android uses `NfcA.transceive()` with a 1,000 ms timeout.
- iOS uses `sendMiFareCommand()` and Core NFC supplies CRC-A.
- Android and iOS prefer 128-byte DATA payloads, fall back to 64 bytes after a
  transport error, and use an 8 ms gap between DATA commands.
- A tag loss retains the final JPEG, transfer ID and CRC in the app cache.
- Repeating BEGIN resumes at the returned `nextExpectedOffset`.
- The firmware callback only validates, copies and responds; display and file
  work remain asynchronous.
