import 'dart:isolate';
import 'dart:math';
import 'dart:typed_data';

import 'package:image/image.dart' as img;

import '../protocol/checksums.dart';
import '../protocol/paper_mono_protocol.dart';
import 'jpeg_inspector.dart';
import 'paper_mono_image_mode.dart';
import 'prepared_image.dart';

class ImageProcessor {
  const ImageProcessor();

  Future<PreparedImage> prepare(
    Uint8List croppedBytes,
    PaperMonoImageMode mode,
  ) async {
    final result = await Isolate.run(
      () => _prepareInIsolate(croppedBytes, mode),
    );
    return result;
  }

  static PreparedImage _prepareInIsolate(
    Uint8List croppedBytes,
    PaperMonoImageMode mode,
  ) {
    final decoded = img.decodeImage(croppedBytes);
    if (decoded == null) {
      throw const FormatException('選択された画像をデコードできませんでした。');
    }

    final resized = img.copyResize(
      decoded,
      width: mode.width,
      height: mode.height,
      interpolation: img.Interpolation.cubic,
    );

    // A fresh 3-channel image deliberately strips EXIF/ICC metadata and makes
    // the JPEG encoder write the same 3-component baseline shape as existing
    // PaperMono travel assets.
    final rgb = img.Image(
      width: mode.width,
      height: mode.height,
      numChannels: 3,
    );
    for (final pixel in resized) {
      final luminance = (0.299 * pixel.r + 0.587 * pixel.g + 0.114 * pixel.b)
          .round();
      rgb.setPixelRgb(pixel.x, pixel.y, luminance, luminance, luminance);
    }

    Uint8List? jpeg;
    for (final quality in const <int>[80, 75, 70, 65, 60]) {
      final candidate = img.encodeJpg(
        rgb,
        quality: quality,
        chroma: img.JpegChroma.yuv444,
      );
      if (candidate.length <= PaperMonoProtocol.maxImageBytes) {
        jpeg = candidate;
        break;
      }
    }
    if (jpeg == null) {
      throw const FormatException('JPEGを256KB以下に圧縮できませんでした。');
    }

    final info = JpegInspector.inspect(jpeg);
    if (!info.isPaperMonoV1Compatible ||
        info.width != mode.width ||
        info.height != mode.height) {
      throw const FormatException('生成されたJPEGがPaperMono v1仕様に適合しません。');
    }
    final verification = img.decodeJpg(jpeg);
    if (verification == null ||
        verification.width != mode.width ||
        verification.height != mode.height) {
      throw const FormatException('生成されたJPEGの再デコード検証に失敗しました。');
    }

    return PreparedImage(
      bytes: jpeg,
      mode: mode,
      crc32: crc32IsoHdlc(jpeg),
      transferId: _newTransferId(),
      createdAt: DateTime.now().toUtc(),
    );
  }

  static int _newTransferId() {
    final random = Random.secure();
    final value = (random.nextInt(1 << 16) << 16) | random.nextInt(1 << 16);
    return value == 0 ? 1 : value;
  }
}
