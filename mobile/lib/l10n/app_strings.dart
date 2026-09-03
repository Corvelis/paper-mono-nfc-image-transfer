import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';

import '../core/image/paper_mono_image_mode.dart';
import '../features/transfer/nfc_transfer_bridge.dart';

class AppStrings {
  const AppStrings(this.locale);

  final Locale locale;

  static const supportedLocales = <Locale>[Locale('ja'), Locale('en')];
  static const delegate = _AppStringsDelegate();

  static AppStrings of(BuildContext context) {
    final strings = Localizations.of<AppStrings>(context, AppStrings);
    assert(strings != null, 'AppStrings delegate is missing.');
    return strings!;
  }

  bool get isEnglish => locale.languageCode == 'en';
  String _text(String ja, String en) => isEnglish ? en : ja;

  String get appTitle => 'Paper Mono Image Sender';
  String get language => _text('言語', 'Language');
  String get japanese => '日本語';
  String get english => 'English';
  String get licenses => _text('ライセンス', 'Licenses');
  String cropFailed(Object cause) =>
      _text('クロップに失敗しました: $cause', 'Cropping failed: $cause');

  String get clockSection => _text('時計を合わせる', 'Set the clock');
  String get clockDescription => _text(
    'スマートフォンの現在時刻とタイムゾーンをNFCで送ります。',
    'Send the phone’s current time and time zone over NFC.',
  );
  String get syncClock => _text('NFCで時刻を同期', 'Sync clock over NFC');
  String get displayLayout => _text('表示レイアウト', 'Display layout');
  String get dashboardLayout => _text('時計と表示', 'Clock + image');
  String get fullScreenLayout => _text('全画面', 'Full screen');
  String modeDescription(PaperMonoImageMode mode) => switch (mode) {
    PaperMonoImageMode.dateTime => _text(
      '画像、時計、カレンダー、歩数と一緒に表示',
      'Show the image with the clock, calendar, and steps',
    ),
    PaperMonoImageMode.fullScreen => _text(
      '画像だけをPaper Monoの画面全体に表示',
      'Show only the image across the entire Paper Mono display',
    ),
  };

  String get chooseImage => _text('1. 画像を選ぶ', '1. Choose an image');
  String get gallery => _text('ギャラリー', 'Gallery');
  String get camera => _text('カメラ', 'Camera');
  String get adjustCrop => _text('2. 切り抜きを調整', '2. Adjust the crop');
  String get generatePreview =>
      _text('モノクロプレビューを生成', 'Generate monochrome preview');
  String get transferPreview => _text('3. 送信プレビュー', '3. Transfer preview');
  String get sendOverNfc => _text('NFCで送信', 'Send over NFC');
  String get close => _text('閉じる', 'Close');
  String get cancelTransfer => _text('送信を中止', 'Cancel transfer');
  String nfcAvailability(bool available) => available
      ? _text('この端末ではNFCを利用できます', 'NFC is available on this phone')
      : _text('この端末ではNFCを利用できません', 'NFC is unavailable on this phone');
  String get introDescription => _text(
    '画像を選び、表示範囲を調整してPaper Monoへ送ります。',
    'Choose an image, adjust the crop, and send it to Paper Mono.',
  );

  String phaseLabel(NfcTransferPhase phase) => switch (phase) {
    NfcTransferPhase.idle => _text('待機中', 'Idle'),
    NfcTransferPhase.waitingForTag => _text(
      'Paper Monoにスマートフォンを当ててください',
      'Hold your phone near Paper Mono',
    ),
    NfcTransferPhase.connected => _text(
      'Paper Monoに接続しました',
      'Connected to Paper Mono',
    ),
    NfcTransferPhase.clockSyncing => _text(
      '時刻を同期しています',
      'Synchronizing the clock',
    ),
    NfcTransferPhase.clockSynced => _text('時刻を同期しました', 'Clock synchronized'),
    NfcTransferPhase.receiving => _text('画像を送信しています', 'Sending the image'),
    NfcTransferPhase.verifying => _text(
      'CRCとJPEGを検証しています',
      'Verifying CRC and JPEG',
    ),
    NfcTransferPhase.stored => _text('画像の保存が完了しました', 'Image stored'),
    NfcTransferPhase.displaying => _text(
      'Paper Monoの画面を更新しています',
      'Updating the Paper Mono display',
    ),
    NfcTransferPhase.completed => _text(
      '送信と画面更新が完了しました',
      'Transfer and display update completed',
    ),
    NfcTransferPhase.recoverableError => _text(
      '接続が切れました。もう一度当ててください',
      'Connection lost. Hold the phone near Paper Mono again',
    ),
    NfcTransferPhase.failed => _text('送信に失敗しました', 'Transfer failed'),
  };

  String errorMessage(String? code, {String? fallback}) {
    final normalized = code?.replaceFirst('FormatException: ', '');
    final message = switch (normalized) {
      'IMAGE_NORMALIZE_FAILED' => _text(
        '画像を切り抜き用データへ変換できませんでした。',
        'The image could not be prepared for cropping.',
      ),
      'IMAGE_DECODE_FAILED' => _text(
        '選択された画像をデコードできませんでした。',
        'The selected image could not be decoded.',
      ),
      'IMAGE_TOO_LARGE_AFTER_COMPRESSION' => _text(
        'JPEGを256 KB以下に圧縮できませんでした。',
        'The JPEG could not be compressed below 256 KB.',
      ),
      'GENERATED_JPEG_INVALID' => _text(
        '生成されたJPEGがPaper Mono v1仕様に適合しません。',
        'The generated JPEG does not meet the Paper Mono v1 specification.',
      ),
      'JPEG_VERIFY_FAILED' => _text(
        '生成されたJPEGの検証に失敗しました。',
        'The generated JPEG could not be verified.',
      ),
      'NFC_UNAVAILABLE' => _text(
        'この端末ではNFCを利用できません。',
        'NFC is unavailable on this phone.',
      ),
      'NFC_DISABLED' => _text('端末のNFCを有効にしてください。', 'Enable NFC on your phone.'),
      'INVALID_ARGUMENTS' => _text(
        '送信パラメータが不足しています。',
        'Transfer parameters are missing.',
      ),
      'INVALID_IMAGE' => _text(
        '画像サイズまたは転送IDが不正です。',
        'The image size or transfer ID is invalid.',
      ),
      'INVALID_TIME' => _text(
        '端末の時刻またはタイムゾーンが不正です。',
        'The phone time or time zone is invalid.',
      ),
      'TRANSFER_IN_PROGRESS' => _text(
        '別のNFC操作が進行中です。先に中止してください。',
        'Another NFC operation is active. Cancel it first.',
      ),
      'TRANSCEIVE_LIMIT_TOO_SMALL' => _text(
        'この端末のNFCコマンド上限が小さすぎます。',
        'This phone’s NFC command limit is too small.',
      ),
      'TIME_SYNC_UNSUPPORTED' => _text(
        'Paper Monoが時刻同期に対応していません。',
        'The Paper Mono firmware does not support clock sync.',
      ),
      'FULLSCREEN_UNSUPPORTED' => _text(
        'Paper Monoが全画面画像に対応していません。',
        'The Paper Mono firmware does not support full-screen images.',
      ),
      'INCOMPATIBLE_LIMITS' || 'UNSAFE_HELLO' => _text(
        'Paper MonoのNFC能力値に互換性がありません。',
        'The Paper Mono NFC capabilities are incompatible.',
      ),
      'INVALID_OFFSET' || 'TRANSFER_STALLED' => _text(
        'Paper Monoから不正な受信位置が返されました。',
        'Paper Mono returned an invalid transfer position.',
      ),
      'TRANSFER_ID_MISMATCH' => _text(
        'Paper Monoの転送IDが一致しません。もう一度当ててください。',
        'The transfer ID does not match. Hold the phone near Paper Mono again.',
      ),
      'COMMIT_TIMEOUT' => _text(
        'Paper Monoの保存確認がタイムアウトしました。',
        'Timed out while waiting for Paper Mono to store the image.',
      ),
      'CRC_MISMATCH' => _text(
        'Paper Monoで画像CRCが一致しませんでした。',
        'The image CRC did not match on Paper Mono.',
      ),
      'INVALID_JPEG' => _text(
        'Paper MonoがJPEGを受け付けませんでした。',
        'Paper Mono rejected the JPEG.',
      ),
      'SHORT_RESPONSE' ||
      'SHORT_HELLO' ||
      'BAD_MAGIC' ||
      'BAD_VERSION' ||
      'BAD_COMMAND' ||
      'UNKNOWN_STATUS' => _text(
        'Paper Monoから不正なNFC応答を受信しました。',
        'Paper Mono returned an invalid NFC response.',
      ),
      'NFC_SESSION_UNAVAILABLE' ||
      'NFC_RESTART_FAILED' ||
      'SESSION_INVALIDATED' => _text(
        'NFCセッションを開始できませんでした。もう一度お試しください。',
        'The NFC session could not be started. Try again.',
      ),
      'TAG_LOST' => _text(
        '接続が切れました。もう一度Paper Monoへ当ててください。',
        'Connection lost. Hold the phone near Paper Mono again.',
      ),
      'CANCELLED' => _text('送信を中止しました。', 'Transfer cancelled.'),
      _ => null,
    };
    if (message != null) return message;
    if (!isEnglish && fallback != null && fallback.isNotEmpty) return fallback;
    final suffix = normalized == null || normalized.isEmpty
        ? ''
        : ' ($normalized)';
    return _text('処理に失敗しました。$suffix', 'The operation failed.$suffix');
  }
}

class _AppStringsDelegate extends LocalizationsDelegate<AppStrings> {
  const _AppStringsDelegate();

  @override
  bool isSupported(Locale locale) => AppStrings.supportedLocales.any(
    (item) => item.languageCode == locale.languageCode,
  );

  @override
  Future<AppStrings> load(Locale locale) =>
      SynchronousFuture(AppStrings(locale));

  @override
  bool shouldReload(_AppStringsDelegate old) => false;
}
