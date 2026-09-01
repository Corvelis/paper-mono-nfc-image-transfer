import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:nfc_image_sender/core/protocol/paper_mono_protocol.dart';

void main() {
  group('PaperMonoProtocol', () {
    test('maximum DATA command is 253 protocol bytes', () {
      final command = PaperMonoProtocol.data(
        transferId: 0x12345678,
        offset: 0x40,
        payload: Uint8List.fromList(
          List<int>.generate(240, (index) => index & 0xff),
        ),
      );

      expect(command.length, 253);
      expect(command.sublist(0, 4), <int>[0x50, 0x4d, 0x01, 0x03]);
      expect(command[12], 240);
      expect(
        command.sublist(13),
        List<int>.generate(240, (index) => index & 0xff),
      );
    });

    test('rejects a DATA payload over 240 bytes', () {
      expect(
        () => PaperMonoProtocol.data(
          transferId: 1,
          offset: 0,
          payload: Uint8List(241),
        ),
        throwsArgumentError,
      );
    });

    test('BEGIN uses fixed little-endian layout', () {
      final command = PaperMonoProtocol.begin(
        transferId: 0x12345678,
        mode: 2,
        width: 480,
        height: 800,
        totalImageBytes: 0x00010203,
        imageCrc32: 0x89abcdef,
      );

      expect(command.length, 23);
      expect(command.sublist(0, 4), <int>[0x50, 0x4d, 0x01, 0x02]);
      expect(command.sublist(4, 8), <int>[0x78, 0x56, 0x34, 0x12]);
      expect(command.sublist(8, 11), <int>[0, 2, 1]);
      expect(command.sublist(11, 15), <int>[0xe0, 0x01, 0x20, 0x03]);
      expect(command.sublist(15, 19), <int>[0x03, 0x02, 0x01, 0x00]);
      expect(command.sublist(19, 23), <int>[0xef, 0xcd, 0xab, 0x89]);
    });

    test('TIME_SET uses Unix seconds and signed UTC offset', () {
      final command = PaperMonoProtocol.setTime(
        unixTimeSeconds: 0x0102030405060708,
        utcOffsetMinutes: 540,
      );

      expect(command.length, 15);
      expect(command.sublist(0, 4), <int>[0x50, 0x4d, 0x01, 0x07]);
      expect(command.sublist(4, 12), <int>[8, 7, 6, 5, 4, 3, 2, 1]);
      expect(command.sublist(12), <int>[0x1c, 0x02, 0]);
    });

    test('parses response envelope and extra bytes', () {
      final bytes = Uint8List.fromList(<int>[
        0x50,
        0x4d,
        0x01,
        0x83,
        0x11,
        0x78,
        0x56,
        0x34,
        0x12,
        0x30,
        0x00,
        0x00,
        0x00,
        0xaa,
      ]);

      final response = PaperMonoProtocol.parseResponse(
        bytes,
        PaperMonoCommand.data,
      );

      expect(response.status, PaperMonoStatus.receiving);
      expect(response.transferId, 0x12345678);
      expect(response.nextExpectedOffset, 48);
      expect(response.extra, <int>[0xaa]);
    });

    test('rejects short and mismatched responses', () {
      expect(
        () => PaperMonoProtocol.parseResponse(
          Uint8List(12),
          PaperMonoCommand.hello,
        ),
        throwsFormatException,
      );
      expect(
        () => PaperMonoProtocol.parseResponse(
          Uint8List.fromList(<int>[
            0x50,
            0x4d,
            0x01,
            0x82,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
          ]),
          PaperMonoCommand.hello,
        ),
        throwsFormatException,
      );
    });
  });
}
