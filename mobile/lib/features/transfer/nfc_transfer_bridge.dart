import 'dart:async';

import 'package:flutter/services.dart';

import '../../core/image/prepared_image.dart';

class NfcTransferBridge {
  const NfcTransferBridge();

  static const MethodChannel _methods = MethodChannel(
    'io.github.corvelis.paper_mono_image_sender/methods',
  );
  static const EventChannel _events = EventChannel(
    'io.github.corvelis.paper_mono_image_sender/events',
  );

  Stream<NfcTransferEvent> get events => _events
      .receiveBroadcastStream()
      .where((event) => event is Map)
      .map(
        (event) =>
            NfcTransferEvent.fromMap(Map<Object?, Object?>.from(event as Map)),
      );

  Future<bool> isAvailable() async {
    return await _methods.invokeMethod<bool>('isAvailable') ?? false;
  }

  Future<void> start(PreparedImage image) async {
    final clock = _currentClockArguments();
    await _methods.invokeMethod<void>('startTransfer', <String, Object>{
      'bytes': image.bytes,
      'mode': image.mode.code,
      'width': image.mode.width,
      'height': image.mode.height,
      'crc32': image.crc32,
      'transferId': image.transferId,
      ...clock,
    });
  }

  Future<void> syncClock() =>
      _methods.invokeMethod<void>('syncClock', _currentClockArguments());

  Future<void> cancel() => _methods.invokeMethod<void>('cancelTransfer');

  Map<String, Object> _currentClockArguments() {
    final now = DateTime.now();
    return <String, Object>{
      'unixTimeSeconds': now.millisecondsSinceEpoch ~/ 1000,
      'utcOffsetMinutes': now.timeZoneOffset.inMinutes,
    };
  }
}

enum NfcTransferPhase {
  idle,
  waitingForTag,
  connected,
  clockSyncing,
  clockSynced,
  receiving,
  verifying,
  stored,
  displaying,
  completed,
  recoverableError,
  failed;

  static NfcTransferPhase fromName(String? name) {
    return values.firstWhere(
      (phase) => phase.name == name,
      orElse: () => failed,
    );
  }
}

class NfcTransferEvent {
  const NfcTransferEvent({
    required this.phase,
    required this.bytesSent,
    required this.totalBytes,
    required this.nextExpectedOffset,
    this.message,
    this.errorCode,
  });

  factory NfcTransferEvent.fromMap(Map<Object?, Object?> map) {
    return NfcTransferEvent(
      phase: NfcTransferPhase.fromName(map['phase'] as String?),
      bytesSent: (map['bytesSent'] as num?)?.toInt() ?? 0,
      totalBytes: (map['totalBytes'] as num?)?.toInt() ?? 0,
      nextExpectedOffset: (map['nextExpectedOffset'] as num?)?.toInt() ?? 0,
      message: map['message'] as String?,
      errorCode: map['errorCode'] as String?,
    );
  }

  final NfcTransferPhase phase;
  final int bytesSent;
  final int totalBytes;
  final int nextExpectedOffset;
  final String? message;
  final String? errorCode;

  double get progress => totalBytes == 0 ? 0 : bytesSent / totalBytes;
}
