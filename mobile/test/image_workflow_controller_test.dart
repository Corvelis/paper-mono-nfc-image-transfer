import 'dart:async';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';

import 'package:nfc_image_sender/core/image/prepared_image.dart';
import 'package:nfc_image_sender/core/image/prepared_image_store.dart';
import 'package:nfc_image_sender/features/home/image_workflow_controller.dart';
import 'package:nfc_image_sender/features/image_editor/image_source_service.dart';
import 'package:nfc_image_sender/features/transfer/nfc_transfer_bridge.dart';

void main() {
  group('ImageWorkflowController transfer status', () {
    test('stored is treated as success and dismissed automatically', () async {
      final nfc = _FakeNfcTransferBridge();
      final store = _FakePreparedImageStore();
      final controller = ImageWorkflowController(
        sourceService: _FakeImageSourceService(),
        store: store,
        nfc: nfc,
        successStatusDuration: const Duration(milliseconds: 20),
      );
      await controller.initialize();

      nfc.emit(_event(NfcTransferPhase.stored));
      await Future<void>.delayed(Duration.zero);

      expect(controller.transferEvent?.phase, NfcTransferPhase.stored);
      expect(store.clearCount, 1);

      await Future<void>.delayed(const Duration(milliseconds: 40));
      expect(controller.transferEvent, isNull);

      controller.dispose();
      await nfc.close();
    });

    test('clockSynced with zero bytes is dismissed automatically', () async {
      final nfc = _FakeNfcTransferBridge();
      final controller = ImageWorkflowController(
        sourceService: _FakeImageSourceService(),
        store: _FakePreparedImageStore(),
        nfc: nfc,
        successStatusDuration: const Duration(milliseconds: 20),
      );
      await controller.initialize();

      nfc.emit(_event(NfcTransferPhase.clockSynced));
      await Future<void>.delayed(Duration.zero);
      expect(controller.transferEvent?.phase, NfcTransferPhase.clockSynced);

      await Future<void>.delayed(const Duration(milliseconds: 40));
      expect(controller.transferEvent, isNull);

      controller.dispose();
      await nfc.close();
    });

    test('failed status remains until the user closes it', () async {
      final nfc = _FakeNfcTransferBridge();
      final controller = ImageWorkflowController(
        sourceService: _FakeImageSourceService(),
        store: _FakePreparedImageStore(),
        nfc: nfc,
        successStatusDuration: const Duration(milliseconds: 20),
      );
      await controller.initialize();

      nfc.emit(
        const NfcTransferEvent(
          phase: NfcTransferPhase.failed,
          bytesSent: 0,
          totalBytes: 0,
          nextExpectedOffset: 0,
          errorCode: 'NFC_TEST_FAILURE',
        ),
      );
      await Future<void>.delayed(const Duration(milliseconds: 40));

      expect(controller.transferEvent?.phase, NfcTransferPhase.failed);
      expect(controller.errorCode, 'NFC_TEST_FAILURE');

      controller.clearTransferStatus();
      expect(controller.transferEvent, isNull);

      controller.dispose();
      await nfc.close();
    });
  });
}

NfcTransferEvent _event(NfcTransferPhase phase) => NfcTransferEvent(
  phase: phase,
  bytesSent: 128,
  totalBytes: 128,
  nextExpectedOffset: 128,
);

class _FakeNfcTransferBridge extends NfcTransferBridge {
  final StreamController<NfcTransferEvent> _events =
      StreamController<NfcTransferEvent>.broadcast();

  @override
  Stream<NfcTransferEvent> get events => _events.stream;

  @override
  Future<bool> isAvailable() async => true;

  void emit(NfcTransferEvent event) => _events.add(event);

  Future<void> close() => _events.close();
}

class _FakePreparedImageStore extends PreparedImageStore {
  int clearCount = 0;

  @override
  Future<PreparedImage?> restore() async => null;

  @override
  Future<void> clear() async {
    clearCount += 1;
  }
}

class _FakeImageSourceService extends ImageSourceService {
  @override
  Future<Uint8List?> recoverLostData() async => null;
}
