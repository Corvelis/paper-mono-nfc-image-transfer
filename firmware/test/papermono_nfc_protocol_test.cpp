#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "PaperMonoNfcProtocol.h"

int main() {
  using namespace PaperMonoNfcProtocol;
  static_assert(kReceiveBufferBytes == 256);
  static_assert(kDataHeaderBytes + kMaxDataPayloadBytes == 253);
  static_assert(kDataHeaderBytes + kMaxDataPayloadBytes + 2 == kMaxFifoFrameBytes);
  static_assert(kMaxFifoFrameBytes < kReceiveBufferBytes);
  static_assert(kBeginRequestBytes <= kMaxCommandBytes);
  static_assert(kResponseHeaderBytes == 13);
  static_assert(static_cast<uint8_t>(Command::Hello) == 0x01);
  static_assert(static_cast<uint8_t>(Command::Begin) == 0x02);
  static_assert(static_cast<uint8_t>(Command::Data) == 0x03);
  static_assert(static_cast<uint8_t>(ImageMode::Dashboard) == 0x01);
  static_assert(static_cast<uint8_t>(ImageMode::Fullscreen) == 0x02);
  static_assert(static_cast<uint8_t>(Command::Status) == 0x04);
  static_assert(static_cast<uint8_t>(Command::Commit) == 0x05);
  static_assert(static_cast<uint8_t>(Command::Abort) == 0x06);
  static_assert(static_cast<uint8_t>(Command::SetTime) == 0x07);
  static_assert(kSetTimeRequestBytes == 15);
  static_assert(static_cast<uint8_t>(Status::Receiving) == 0x11);
  static_assert(static_cast<uint8_t>(Status::Completed) == 0x15);

  const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  assert(crc32(check, sizeof(check)) == 0xCBF43926UL);

  uint8_t encoded[4] = {};
  writeLe32(encoded, 0x78563412UL);
  const uint8_t expected[] = {0x12, 0x34, 0x56, 0x78};
  assert(memcmp(encoded, expected, sizeof(expected)) == 0);
  assert(readLe32(encoded) == 0x78563412UL);

  uint8_t encoded64[8] = {};
  writeLe64(encoded64, 0x0102030405060708ULL);
  const uint8_t expected64[] = {0x08, 0x07, 0x06, 0x05,
                                0x04, 0x03, 0x02, 0x01};
  assert(memcmp(encoded64, expected64, sizeof(expected64)) == 0);
  assert(readLe64(encoded64) == 0x0102030405060708ULL);
  return 0;
}
