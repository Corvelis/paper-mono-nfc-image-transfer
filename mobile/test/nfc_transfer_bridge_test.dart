import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:nfc_image_sender/core/image/paper_mono_image_mode.dart';
import 'package:nfc_image_sender/core/image/prepared_image.dart';
import 'package:nfc_image_sender/features/transfer/nfc_transfer_bridge.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  const channel = MethodChannel(
    'io.github.corvelis.paper_mono_image_sender/methods',
  );
  const bridge = NfcTransferBridge();
  MethodCall? captured;

  setUp(() {
    captured = null;
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (call) async {
          captured = call;
          return null;
        });
  });

  tearDown(() {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, null);
  });

  test('image transfer does not include clock arguments', () async {
    final image = PreparedImage(
      bytes: Uint8List.fromList(<int>[1, 2, 3]),
      mode: PaperMonoImageMode.dateTime,
      crc32: 0x12345678,
      transferId: 0x10203040,
      createdAt: DateTime.utc(2026),
    );

    await bridge.start(image, languageCode: 'en');

    expect(captured?.method, 'startTransfer');
    final arguments = Map<Object?, Object?>.from(captured?.arguments as Map);
    expect(arguments['transferId'], image.transferId);
    expect(arguments['language'], 'en');
    expect(arguments.containsKey('unixTimeSeconds'), isFalse);
    expect(arguments.containsKey('utcOffsetMinutes'), isFalse);
  });

  test('explicit clock sync includes current clock arguments', () async {
    await bridge.syncClock(languageCode: 'ja');

    expect(captured?.method, 'syncClock');
    final arguments = Map<Object?, Object?>.from(captured?.arguments as Map);
    expect(arguments['unixTimeSeconds'], isA<int>());
    expect(arguments['utcOffsetMinutes'], isA<int>());
    expect(arguments['language'], 'ja');
  });
}
