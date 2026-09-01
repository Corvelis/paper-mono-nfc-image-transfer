import 'dart:typed_data';

int crc32IsoHdlc(List<int> bytes) {
  var crc = 0xffffffff;
  for (final byte in bytes) {
    crc ^= byte;
    for (var bit = 0; bit < 8; bit++) {
      crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xedb88320 : crc >> 1;
    }
  }
  return (crc ^ 0xffffffff) & 0xffffffff;
}

/// Returns CRC-A in NFC wire order: low byte, then high byte.
Uint8List appendCrcA(List<int> command) {
  var crc = 0x6363;
  for (final byte in command) {
    crc ^= byte;
    for (var bit = 0; bit < 8; bit++) {
      crc = (crc & 1) != 0 ? (crc >> 1) ^ 0x8408 : crc >> 1;
    }
  }
  return Uint8List.fromList(<int>[...command, crc & 0xff, (crc >> 8) & 0xff]);
}
