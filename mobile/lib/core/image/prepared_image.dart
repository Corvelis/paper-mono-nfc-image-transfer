import 'dart:typed_data';

import 'paper_mono_image_mode.dart';

class PreparedImage {
  const PreparedImage({
    required this.bytes,
    required this.mode,
    required this.crc32,
    required this.transferId,
    required this.createdAt,
  });

  final Uint8List bytes;
  final PaperMonoImageMode mode;
  final int crc32;
  final int transferId;
  final DateTime createdAt;
}
