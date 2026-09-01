import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';
import 'package:nfc_image_sender/core/protocol/checksums.dart';

void main() {
  group('checksums', () {
    test('CRC-32/ISO-HDLC check value', () {
      expect(crc32IsoHdlc(ascii.encode('123456789')), 0xcbf43926);
    });

    test('CRC-A MIFARE read-page vector', () {
      expect(appendCrcA(<int>[0x30, 0x04]), <int>[0x30, 0x04, 0x26, 0xee]);
    });

    test('CRC-A PaperMono HELLO vector', () {
      expect(appendCrcA(<int>[0x50, 0x4d, 0x01, 0x01]), <int>[
        0x50,
        0x4d,
        0x01,
        0x01,
        0x4e,
        0x72,
      ]);
    });
  });
}
