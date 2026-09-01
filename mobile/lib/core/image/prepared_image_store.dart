import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:path/path.dart' as p;
import 'package:path_provider/path_provider.dart';

import 'paper_mono_image_mode.dart';
import 'prepared_image.dart';

class PreparedImageStore {
  const PreparedImageStore();

  Future<Directory> _pendingDirectory() async {
    final support = await getApplicationSupportDirectory();
    return Directory(p.join(support.path, 'paper_mono', 'pending'));
  }

  Future<void> save(PreparedImage image) async {
    final directory = await _pendingDirectory();
    await directory.create(recursive: true);
    await File(
      p.join(directory.path, 'image.jpg'),
    ).writeAsBytes(image.bytes, flush: true);
    await File(p.join(directory.path, 'transfer.json')).writeAsString(
      jsonEncode(<String, Object>{
        'mode': image.mode.name,
        'crc32': image.crc32,
        'transferId': image.transferId,
        'createdAt': image.createdAt.toIso8601String(),
      }),
      flush: true,
    );
  }

  Future<PreparedImage?> restore() async {
    final directory = await _pendingDirectory();
    final imageFile = File(p.join(directory.path, 'image.jpg'));
    final metadataFile = File(p.join(directory.path, 'transfer.json'));
    if (!await imageFile.exists() || !await metadataFile.exists()) {
      return null;
    }
    try {
      final metadata =
          jsonDecode(await metadataFile.readAsString()) as Map<String, dynamic>;
      return PreparedImage(
        bytes: Uint8List.fromList(await imageFile.readAsBytes()),
        mode: PaperMonoImageMode.fromName(metadata['mode'] as String),
        crc32: metadata['crc32'] as int,
        transferId: metadata['transferId'] as int,
        createdAt: DateTime.parse(metadata['createdAt'] as String),
      );
    } on Object {
      await clear();
      return null;
    }
  }

  Future<void> clear() async {
    final directory = await _pendingDirectory();
    if (await directory.exists()) {
      await directory.delete(recursive: true);
    }
  }
}
