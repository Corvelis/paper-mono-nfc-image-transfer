import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:image_picker/image_picker.dart';

import '../../core/image/image_processor.dart';
import '../../core/image/paper_mono_image_mode.dart';
import '../../core/image/prepared_image.dart';
import '../../core/image/prepared_image_store.dart';
import '../image_editor/image_source_service.dart';
import '../transfer/nfc_transfer_bridge.dart';

class ImageWorkflowController extends ChangeNotifier {
  ImageWorkflowController({
    ImageSourceService? sourceService,
    ImageProcessor? processor,
    PreparedImageStore? store,
    NfcTransferBridge? nfc,
  }) : _sourceService = sourceService ?? ImageSourceService(),
       _processor = processor ?? const ImageProcessor(),
       _store = store ?? const PreparedImageStore(),
       _nfc = nfc ?? const NfcTransferBridge();

  final ImageSourceService _sourceService;
  final ImageProcessor _processor;
  final PreparedImageStore _store;
  final NfcTransferBridge _nfc;

  StreamSubscription<NfcTransferEvent>? _transferSubscription;

  Uint8List? sourceBytes;
  PreparedImage? preparedImage;
  PaperMonoImageMode mode = PaperMonoImageMode.dateTime;
  NfcTransferEvent? transferEvent;
  bool isBusy = false;
  bool nfcAvailable = true;
  String? errorMessage;

  bool get isTransferSessionActive => switch (transferEvent?.phase) {
    NfcTransferPhase.waitingForTag ||
    NfcTransferPhase.connected ||
    NfcTransferPhase.clockSyncing ||
    NfcTransferPhase.receiving ||
    NfcTransferPhase.verifying ||
    NfcTransferPhase.stored ||
    NfcTransferPhase.displaying ||
    NfcTransferPhase.recoverableError => true,
    _ => false,
  };

  Future<void> initialize() async {
    _transferSubscription = _nfc.events.listen(_onTransferEvent);
    try {
      nfcAvailable = await _nfc.isAvailable();
      preparedImage = await _store.restore();
      if (preparedImage != null) {
        mode = preparedImage!.mode;
      }
      final recovered = await _sourceService.recoverLostData();
      if (recovered != null) {
        sourceBytes = recovered;
        preparedImage = null;
      }
    } on Object catch (error) {
      errorMessage = _messageFor(error);
    }
    notifyListeners();
  }

  Future<void> pick(ImageSource source) async {
    await _guard(() async {
      final picked = await _sourceService.pick(source);
      if (picked != null) {
        sourceBytes = picked;
        preparedImage = null;
        transferEvent = null;
      }
    });
  }

  void setMode(PaperMonoImageMode newMode) {
    if (mode == newMode) {
      return;
    }
    mode = newMode;
    preparedImage = null;
    transferEvent = null;
    notifyListeners();
  }

  Future<void> prepare(Uint8List croppedBytes) async {
    await _guard(() async {
      final image = await _processor.prepare(croppedBytes, mode);
      await _store.save(image);
      preparedImage = image;
      transferEvent = null;
    });
  }

  Future<void> send() async {
    if (isTransferSessionActive) {
      debugPrint('[nfc.flutter] send ignored: transfer session already active');
      return;
    }
    final image = preparedImage;
    if (image == null) {
      debugPrint('[nfc.flutter] send ignored: no prepared image');
      return;
    }
    debugPrint(
      '[nfc.flutter] send tapped id=${image.transferId} bytes=${image.bytes.length}',
    );
    errorMessage = null;
    transferEvent = NfcTransferEvent(
      phase: NfcTransferPhase.waitingForTag,
      bytesSent: 0,
      totalBytes: image.bytes.length,
      nextExpectedOffset: 0,
      message: 'NFC送信を開始しています。',
    );
    notifyListeners();
    try {
      await _nfc.start(image);
    } on Object catch (error) {
      errorMessage = _messageFor(error);
      transferEvent = NfcTransferEvent(
        phase: NfcTransferPhase.failed,
        bytesSent: 0,
        totalBytes: image.bytes.length,
        nextExpectedOffset: 0,
        message: errorMessage,
      );
      notifyListeners();
    }
  }

  Future<void> syncClock() async {
    if (isTransferSessionActive) {
      return;
    }
    errorMessage = null;
    transferEvent = const NfcTransferEvent(
      phase: NfcTransferPhase.waitingForTag,
      bytesSent: 0,
      totalBytes: 0,
      nextExpectedOffset: 0,
      message: '時刻同期を開始しています。',
    );
    notifyListeners();
    try {
      await _nfc.syncClock();
    } on Object catch (error) {
      errorMessage = _messageFor(error);
      transferEvent = NfcTransferEvent(
        phase: NfcTransferPhase.failed,
        bytesSent: 0,
        totalBytes: 0,
        nextExpectedOffset: 0,
        message: errorMessage,
      );
      notifyListeners();
    }
  }

  Future<void> cancelTransfer() => _nfc.cancel();

  void clearError() {
    errorMessage = null;
    notifyListeners();
  }

  void clearTransferStatus() {
    if (isTransferSessionActive) {
      return;
    }
    transferEvent = null;
    notifyListeners();
  }

  void _onTransferEvent(NfcTransferEvent event) {
    transferEvent = event;
    if (event.phase == NfcTransferPhase.stored ||
        event.phase == NfcTransferPhase.completed) {
      unawaited(_store.clear());
    }
    if (event.phase == NfcTransferPhase.failed) {
      errorMessage = event.message ?? 'NFC送信に失敗しました。';
    }
    notifyListeners();
  }

  Future<void> _guard(Future<void> Function() operation) async {
    isBusy = true;
    errorMessage = null;
    notifyListeners();
    try {
      await operation();
    } on Object catch (error) {
      errorMessage = _messageFor(error);
    } finally {
      isBusy = false;
      notifyListeners();
    }
  }

  String _messageFor(Object error) {
    if (error is PlatformException) {
      return error.message ?? error.code;
    }
    return error.toString().replaceFirst('FormatException: ', '');
  }

  @override
  void dispose() {
    unawaited(_transferSubscription?.cancel());
    super.dispose();
  }
}
