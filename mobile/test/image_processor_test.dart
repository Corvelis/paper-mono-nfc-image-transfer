import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:image/image.dart' as img;
import 'package:nfc_image_sender/core/image/image_processor.dart';
import 'package:nfc_image_sender/core/image/jpeg_inspector.dart';
import 'package:nfc_image_sender/core/image/paper_mono_image_mode.dart';
import 'package:nfc_image_sender/core/protocol/checksums.dart';
import 'package:nfc_image_sender/core/protocol/paper_mono_protocol.dart';

void main() {
  test('prepares a verified 3-component Baseline JPEG', () async {
    final source = img.Image(width: 40, height: 24, numChannels: 3);
    for (final pixel in source) {
      source.setPixelRgb(
        pixel.x,
        pixel.y,
        pixel.x * 6,
        pixel.y * 10,
        (pixel.x + pixel.y) * 4,
      );
    }

    final prepared = await const ImageProcessor().prepare(
      Uint8List.fromList(img.encodePng(source)),
      PaperMonoImageMode.dateTime,
    );
    final info = JpegInspector.inspect(prepared.bytes);

    expect(info.width, 386);
    expect(info.height, 386);
    expect(info.precision, 8);
    expect(info.components, 3);
    expect(info.progressive, isFalse);
    expect(
      prepared.bytes.length,
      lessThanOrEqualTo(PaperMonoProtocol.maxImageBytes),
    );
    expect(prepared.crc32, crc32IsoHdlc(prepared.bytes));
    expect(prepared.transferId, isNonZero);
  });

  test('prepares a verified 480x800 full-screen JPEG', () async {
    final source = img.Image(width: 60, height: 100, numChannels: 3);
    for (final pixel in source) {
      final value = (pixel.x * 3 + pixel.y * 2) & 0xff;
      source.setPixelRgb(pixel.x, pixel.y, value, value, value);
    }
    final prepared = await const ImageProcessor().prepare(
      Uint8List.fromList(img.encodePng(source)),
      PaperMonoImageMode.fullScreen,
    );
    final info = JpegInspector.inspect(prepared.bytes);
    expect(info.width, 480);
    expect(info.height, 800);
    expect(info.precision, 8);
    expect(info.components, 3);
    expect(info.progressive, isFalse);
    expect(
      prepared.bytes.length,
      lessThanOrEqualTo(PaperMonoProtocol.maxImageBytes),
    );
  });
}
