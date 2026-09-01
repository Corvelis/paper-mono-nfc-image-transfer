#pragma once

#include <stddef.h>
#include <stdint.h>

namespace PaperMonoNfcProtocol {

constexpr uint8_t kMagic0 = 0x50;  // 'P'
constexpr uint8_t kMagic1 = 0x4D;  // 'M'
constexpr uint8_t kVersion = 1;

// The production build expands M5Unit-NFC's emulation receive buffers to 256
// bytes. Keep one byte of headroom: the largest accepted RF frame is a
// 253-byte protocol command plus the two CRC-A bytes consumed by the NFC layer.
// CRC-A is deliberately not part of protocol command lengths.
constexpr size_t kReceiveBufferBytes = 256;
constexpr size_t kMaxFifoFrameBytes = 255;
constexpr size_t kMaxCommandBytes = 253;
constexpr size_t kDataHeaderBytes = 13;
constexpr size_t kMaxDataPayloadBytes = 240;
constexpr uint32_t kMaxImageBytes = 262144;
constexpr uint32_t kTransferTtlMs = 120000;

constexpr size_t kCommonHeaderBytes = 4;
constexpr size_t kTransferRequestBytes = 8;
constexpr size_t kBeginRequestBytes = 23;
constexpr size_t kSetTimeRequestBytes = 15;
constexpr size_t kResponseHeaderBytes = 13;

enum class Command : uint8_t {
  Hello = 0x01,
  Begin = 0x02,
  Data = 0x03,
  Status = 0x04,
  Commit = 0x05,
  Abort = 0x06,
  SetTime = 0x07,
};

enum class Status : uint8_t {
  Ok = 0x00,
  Accepted = 0x01,
  Busy = 0x02,
  Conflict = 0x03,
  BadMagic = 0x04,
  UnsupportedVersion = 0x05,
  UnknownCommand = 0x06,
  InvalidLength = 0x07,
  PayloadTooLarge = 0x08,
  ImageTooLarge = 0x09,
  BadOffset = 0x0A,
  DataMismatch = 0x0B,
  CrcMismatch = 0x0C,
  InvalidJpeg = 0x0D,
  UnsupportedFormat = 0x0E,
  NotFound = 0x0F,
  InternalError = 0x10,
  Receiving = 0x11,
  Verifying = 0x12,
  Stored = 0x13,
  Displaying = 0x14,
  Completed = 0x15,

  // Internal failure categories intentionally collapse to v1's INTERNAL_ERROR
  // on the wire. Keep these aliases so call sites retain useful intent.
  InvalidArgument = InvalidLength,
  NoMemory = InternalError,
  StorageError = InternalError,
  HardwareError = InternalError,
};

enum class Phase : uint8_t {
  Idle = 0,
  Receiving = 1,
  Verifying = 2,
  Persisting = 3,
  Stored = 4,
  Displaying = 5,
  Completed = 6,
  Error = 7,
};

enum class ImageMode : uint8_t {
  Dashboard = 1,
};

enum class ImageFormat : uint8_t {
  JpegBaselineRgb = 1,
};

enum BeginFlags : uint8_t {
  BeginReplace = 0x01,
};

inline uint16_t readLe16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) |
         (static_cast<uint16_t>(data[1]) << 8);
}

inline uint32_t readLe32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

inline uint64_t readLe64(const uint8_t* data) {
  return static_cast<uint64_t>(readLe32(data)) |
         (static_cast<uint64_t>(readLe32(data + 4)) << 32);
}

inline void writeLe16(uint8_t* data, uint16_t value) {
  data[0] = static_cast<uint8_t>(value);
  data[1] = static_cast<uint8_t>(value >> 8);
}

inline void writeLe32(uint8_t* data, uint32_t value) {
  data[0] = static_cast<uint8_t>(value);
  data[1] = static_cast<uint8_t>(value >> 8);
  data[2] = static_cast<uint8_t>(value >> 16);
  data[3] = static_cast<uint8_t>(value >> 24);
}

inline void writeLe64(uint8_t* data, uint64_t value) {
  writeLe32(data, static_cast<uint32_t>(value));
  writeLe32(data + 4, static_cast<uint32_t>(value >> 32));
}

inline uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t length) {
  while (length-- > 0) {
    crc ^= *data++;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
    }
  }
  return crc;
}

inline uint32_t crc32(const uint8_t* data, size_t length) {
  return crc32Update(0xFFFFFFFFUL, data, length) ^ 0xFFFFFFFFUL;
}

}  // namespace PaperMonoNfcProtocol
