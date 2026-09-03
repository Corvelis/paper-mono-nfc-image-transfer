import 'dart:io';

import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:nfc_image_sender/features/transfer/nfc_transfer_bridge.dart';
import 'package:nfc_image_sender/l10n/app_strings.dart';
import 'package:nfc_image_sender/l10n/language_preference_store.dart';

void main() {
  test('Japanese and English strings cover UI and transfer states', () {
    const japanese = AppStrings(Locale('ja'));
    const english = AppStrings(Locale('en'));

    expect(japanese.displayLayout, '表示レイアウト');
    expect(english.displayLayout, 'Display layout');
    expect(japanese.phaseLabel(NfcTransferPhase.stored), '画像の保存が完了しました');
    expect(english.phaseLabel(NfcTransferPhase.stored), 'Image stored');
    expect(
      english.errorMessage('FULLSCREEN_UNSUPPORTED'),
      contains('full-screen'),
    );
  });

  test(
    'language preference defaults to Japanese and persists English',
    () async {
      final directory = await Directory.systemTemp.createTemp(
        'paper-mono-language-test-',
      );
      final store = LanguagePreferenceStore(
        supportDirectory: () async => directory,
      );
      try {
        expect((await store.load()).languageCode, 'ja');
        await store.save(const Locale('en'));
        expect((await store.load()).languageCode, 'en');
        await store.save(const Locale('fr'));
        expect((await store.load()).languageCode, 'ja');
      } finally {
        await directory.delete(recursive: true);
      }
    },
  );
}
