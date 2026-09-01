import 'dart:typed_data';

class JpegInfo {
  const JpegInfo({
    required this.width,
    required this.height,
    required this.precision,
    required this.components,
    required this.progressive,
  });

  final int width;
  final int height;
  final int precision;
  final int components;
  final bool progressive;

  bool get isPaperMonoV1Compatible =>
      !progressive && precision == 8 && components == 3;
}

abstract final class JpegInspector {
  static JpegInfo inspect(Uint8List bytes) {
    if (bytes.length < 4 || bytes[0] != 0xff || bytes[1] != 0xd8) {
      throw const FormatException('JPEG SOI marker is missing');
    }

    var offset = 2;
    while (offset + 3 < bytes.length) {
      while (offset < bytes.length && bytes[offset] != 0xff) {
        offset++;
      }
      while (offset < bytes.length && bytes[offset] == 0xff) {
        offset++;
      }
      if (offset >= bytes.length) {
        break;
      }

      final marker = bytes[offset++];
      if (marker == 0xd9 || marker == 0xda) {
        break;
      }
      if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) {
        continue;
      }
      if (offset + 1 >= bytes.length) {
        throw const FormatException('Truncated JPEG segment length');
      }

      final segmentLength = (bytes[offset] << 8) | bytes[offset + 1];
      if (segmentLength < 2 || offset + segmentLength > bytes.length) {
        throw const FormatException('Invalid JPEG segment length');
      }

      if (marker == 0xc0 || marker == 0xc2) {
        if (segmentLength < 8) {
          throw const FormatException('Invalid JPEG SOF segment');
        }
        return JpegInfo(
          precision: bytes[offset + 2],
          height: (bytes[offset + 3] << 8) | bytes[offset + 4],
          width: (bytes[offset + 5] << 8) | bytes[offset + 6],
          components: bytes[offset + 7],
          progressive: marker == 0xc2,
        );
      }
      offset += segmentLength;
    }
    throw const FormatException('JPEG SOF0/SOF2 marker is missing');
  }
}
