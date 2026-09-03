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
    Duration successStatusDuration = const Duration(seconds: 2),
  }) : _sourceService = sourceService ?? ImageSourceService(),
       _processor = processor ?? const ImageProcessor(),
       _store = store ?? const PreparedImageStore(),
       _nfc = nfc ?? const NfcTransferBridge(),
       _successStatusDuration = successStatusDuration;

  final ImageSourceService _sourceService;
  final ImageProcessor _processor;
  final PreparedImageStore _store;
  final NfcTransferBridge _nfc;
  final Duration _successStatusDuration;

  StreamSubscription<NfcTransferEvent>? _transferSubscription;
  Timer? _statusDismissTimer;
  bool _successStatusDismissed = false;

  Uint8List? sourceBytes;
  PreparedImage? preparedImage;
  PaperMonoImageMode mode = PaperMonoImageMode.dateTime;
  NfcTransferEvent? transferEvent;
  bool isBusy = false;
  bool nfcAvailable = true;
  String? errorMessage;
  String? errorCode;
  String languageCode = 'ja';

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
      _captureError(error);
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

  void setLanguage(String code) {
    languageCode = code == 'en' ? 'en' : 'ja';
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
    errorCode = null;
    _statusDismissTimer?.cancel();
    _successStatusDismissed = false;
    transferEvent = NfcTransferEvent(
      phase: NfcTransferPhase.waitingForTag,
      bytesSent: 0,
      totalBytes: image.bytes.length,
      nextExpectedOffset: 0,
    );
    notifyListeners();
    try {
      await _nfc.start(image, languageCode: languageCode);
    } on Object catch (error) {
      _captureError(error);
      transferEvent = NfcTransferEvent(
        phase: NfcTransferPhase.failed,
        bytesSent: 0,
        totalBytes: image.bytes.length,
        nextExpectedOffset: 0,
        message: errorMessage,
        errorCode: errorCode,
      );
      notifyListeners();
    }
  }

  Future<void> syncClock() async {
    if (isTransferSessionActive) {
      return;
    }
    errorMessage = null;
    errorCode = null;
    _statusDismissTimer?.cancel();
    _successStatusDismissed = false;
    transferEvent = const NfcTransferEvent(
      phase: NfcTransferPhase.waitingForTag,
      bytesSent: 0,
      totalBytes: 0,
      nextExpectedOffset: 0,
    );
    notifyListeners();
    try {
      await _nfc.syncClock(languageCode: languageCode);
    } on Object catch (error) {
      _captureError(error);
      transferEvent = NfcTransferEvent(
        phase: NfcTransferPhase.failed,
        bytesSent: 0,
        totalBytes: 0,
        nextExpectedOffset: 0,
        message: errorMessage,
        errorCode: errorCode,
      );
      notifyListeners();
    }
  }

  Future<void> cancelTransfer() => _nfc.cancel();

  void clearError() {
    errorMessage = null;
    errorCode = null;
    notifyListeners();
  }

  void clearTransferStatus() {
    final phase = transferEvent?.phase;
    if (isTransferSessionActive && !_isSuccessfulTerminal(phase)) {
      return;
    }
    _statusDismissTimer?.cancel();
    if (_isSuccessfulTerminal(phase)) _successStatusDismissed = true;
    transferEvent = null;
    notifyListeners();
  }

  void _onTransferEvent(NfcTransferEvent event) {
    if (_successStatusDismissed &&
        transferEvent == null &&
        _isSuccessfulTerminal(event.phase)) {
      return;
    }
    _statusDismissTimer?.cancel();
    if (event.phase == NfcTransferPhase.idle) {
      transferEvent = null;
      notifyListeners();
      return;
    }
    transferEvent = event;
    if (event.phase == NfcTransferPhase.stored ||
        event.phase == NfcTransferPhase.displaying ||
        event.phase == NfcTransferPhase.completed) {
      unawaited(_store.clear());
    }
    if (event.phase == NfcTransferPhase.failed) {
      errorCode = event.errorCode ?? 'NFC_SEND_FAILED';
      errorMessage = event.message;
    }
    if (_isSuccessfulTerminal(event.phase)) {
      _statusDismissTimer = Timer(_successStatusDuration, () {
        if (identical(transferEvent, event)) {
          _successStatusDismissed = true;
          transferEvent = null;
          notifyListeners();
        }
      });
    }
    notifyListeners();
  }

  bool _isSuccessfulTerminal(NfcTransferPhase? phase) =>
      phase == NfcTransferPhase.stored ||
      phase == NfcTransferPhase.displaying ||
      phase == NfcTransferPhase.completed ||
      phase == NfcTransferPhase.clockSynced;

  Future<void> _guard(Future<void> Function() operation) async {
    isBusy = true;
    errorMessage = null;
    errorCode = null;
    notifyListeners();
    try {
      await operation();
    } on Object catch (error) {
      _captureError(error);
    } finally {
      isBusy = false;
      notifyListeners();
    }
  }

  void _captureError(Object error) {
    if (error is PlatformException) {
      errorCode = error.code;
      errorMessage = error.message;
      return;
    }
    if (error is FormatException) {
      errorCode = error.message;
      errorMessage = null;
      return;
    }
    errorCode = 'INTERNAL_ERROR';
    errorMessage = error.toString();
  }

  @override
  void dispose() {
    _statusDismissTimer?.cancel();
    unawaited(_transferSubscription?.cancel());
    super.dispose();
  }
}
