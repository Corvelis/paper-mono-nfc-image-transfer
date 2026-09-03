import 'dart:typed_data';

import 'package:flutter_image_compress/flutter_image_compress.dart';
import 'package:image_picker/image_picker.dart';

class ImageSourceService {
  ImageSourceService({ImagePicker? picker}) : _picker = picker ?? ImagePicker();

  final ImagePicker _picker;

  Future<Uint8List?> pick(ImageSource source) async {
    final file = await _picker.pickImage(
      source: source,
      requestFullMetadata: false,
    );
    return file == null ? null : normalize(file);
  }

  Future<Uint8List?> recoverLostData() async {
    final response = await _picker.retrieveLostData();
    final files = response.files;
    if (response.isEmpty || files == null || files.isEmpty) {
      return null;
    }
    return normalize(files.first);
  }

  Future<Uint8List> normalize(XFile file) async {
    final normalized = await FlutterImageCompress.compressWithFile(
      file.path,
      minWidth: 1600,
      minHeight: 1600,
      rotate: 0,
      autoCorrectionAngle: true,
      // Android 14+ may preserve an Ultra HDR gainmap when a Bitmap is
      // compressed back to JPEG. crop_your_image's Dart decoder cannot parse
      // that extended JPEG and remains on its loading indicator. PNG forces
      // the native decoder to flatten the gainmap while still normalizing
      // HEIC/orientation. The final transfer image is encoded separately as a
      // three-component Baseline JPEG after cropping.
      format: CompressFormat.png,
      keepExif: false,
    );
    if (normalized == null || normalized.isEmpty) {
      throw const FormatException('IMAGE_NORMALIZE_FAILED');
    }
    return normalized;
  }
}
