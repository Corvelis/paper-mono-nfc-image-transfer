import 'dart:typed_data';

abstract final class PaperMonoProtocol {
  static const int magic0 = 0x50; // P
  static const int magic1 = 0x4d; // M
  static const int version = 1;

  static const int rxBufferBytes = 256;
  static const int maxAcceptedRfFrameBytes = 255;
  static const int maxProtocolCommandBytes = 253;
  static const int maxDataPayloadBytes = 240;
  static const int maxImageBytes = 262144;

  static const int responseEnvelopeBytes = 13;
  static const int dataHeaderBytes = 13;

  static Uint8List hello() => Uint8List.fromList(<int>[
    magic0,
    magic1,
    version,
    PaperMonoCommand.hello.code,
  ]);

  static Uint8List begin({
    required int transferId,
    required int mode,
    required int width,
    required int height,
    required int totalImageBytes,
    required int imageCrc32,
    bool replace = false,
  }) {
    final data = ByteData(23);
    _writePrefix(data, PaperMonoCommand.begin.code);
    data.setUint32(4, transferId, Endian.little);
    data.setUint8(8, replace ? 1 : 0);
    data.setUint8(9, mode);
    data.setUint8(10, PaperMonoImageFormat.baselineJpeg3Component.code);
    data.setUint16(11, width, Endian.little);
    data.setUint16(13, height, Endian.little);
    data.setUint32(15, totalImageBytes, Endian.little);
    data.setUint32(19, imageCrc32, Endian.little);
    return data.buffer.asUint8List();
  }

  static Uint8List data({
    required int transferId,
    required int offset,
    required Uint8List payload,
  }) {
    if (payload.length > maxDataPayloadBytes) {
      throw ArgumentError.value(
        payload.length,
        'payload',
        'DATA payload must be at most $maxDataPayloadBytes bytes',
      );
    }
    final result = ByteData(dataHeaderBytes + payload.length);
    _writePrefix(result, PaperMonoCommand.data.code);
    result.setUint32(4, transferId, Endian.little);
    result.setUint32(8, offset, Endian.little);
    result.setUint8(12, payload.length);
    result.buffer.asUint8List().setRange(
      dataHeaderBytes,
      result.lengthInBytes,
      payload,
    );
    return result.buffer.asUint8List();
  }

  static Uint8List status(int transferId) {
    final data = ByteData(8);
    _writePrefix(data, PaperMonoCommand.status.code);
    data.setUint32(4, transferId, Endian.little);
    return data.buffer.asUint8List();
  }

  static Uint8List commit(int transferId, int imageCrc32) {
    final data = ByteData(12);
    _writePrefix(data, PaperMonoCommand.commit.code);
    data.setUint32(4, transferId, Endian.little);
    data.setUint32(8, imageCrc32, Endian.little);
    return data.buffer.asUint8List();
  }

  static Uint8List abort(int transferId) {
    final data = ByteData(8);
    _writePrefix(data, PaperMonoCommand.abort.code);
    data.setUint32(4, transferId, Endian.little);
    return data.buffer.asUint8List();
  }

  static Uint8List setTime({
    required int unixTimeSeconds,
    required int utcOffsetMinutes,
    int flags = 0,
  }) {
    if (unixTimeSeconds < 0) {
      throw ArgumentError.value(unixTimeSeconds, 'unixTimeSeconds');
    }
    if (utcOffsetMinutes < -840 || utcOffsetMinutes > 840) {
      throw ArgumentError.value(utcOffsetMinutes, 'utcOffsetMinutes');
    }
    final data = ByteData(15);
    _writePrefix(data, PaperMonoCommand.setTime.code);
    data.setUint64(4, unixTimeSeconds, Endian.little);
    data.setInt16(12, utcOffsetMinutes, Endian.little);
    data.setUint8(14, flags);
    return data.buffer.asUint8List();
  }

  static PaperMonoResponse parseResponse(
    Uint8List bytes,
    PaperMonoCommand expected,
  ) {
    if (bytes.length < responseEnvelopeBytes) {
      throw const FormatException(
        'PaperMono response is shorter than 13 bytes',
      );
    }
    final data = ByteData.sublistView(bytes);
    if (data.getUint8(0) != magic0 || data.getUint8(1) != magic1) {
      throw const FormatException('PaperMono response magic does not match');
    }
    if (data.getUint8(2) != version) {
      throw FormatException('Unsupported protocol version ${data.getUint8(2)}');
    }
    final responseCommand = data.getUint8(3);
    if (responseCommand != (expected.code | 0x80)) {
      throw FormatException(
        'Unexpected response command 0x${responseCommand.toRadixString(16)}',
      );
    }
    return PaperMonoResponse(
      command: expected,
      status: PaperMonoStatus.fromCode(data.getUint8(4)),
      transferId: data.getUint32(5, Endian.little),
      nextExpectedOffset: data.getUint32(9, Endian.little),
      extra: Uint8List.sublistView(bytes, responseEnvelopeBytes),
    );
  }

  static void _writePrefix(ByteData data, int command) {
    data.setUint8(0, magic0);
    data.setUint8(1, magic1);
    data.setUint8(2, version);
    data.setUint8(3, command);
  }
}

enum PaperMonoCommand {
  hello(0x01),
  begin(0x02),
  data(0x03),
  status(0x04),
  commit(0x05),
  abort(0x06),
  setTime(0x07);

  const PaperMonoCommand(this.code);
  final int code;
}

enum PaperMonoImageFormat {
  baselineJpeg3Component(0x01);

  const PaperMonoImageFormat(this.code);
  final int code;
}

enum PaperMonoStatus {
  ok(0x00),
  accepted(0x01),
  busy(0x02),
  conflict(0x03),
  badMagic(0x04),
  unsupportedVersion(0x05),
  unknownCommand(0x06),
  invalidLength(0x07),
  payloadTooLarge(0x08),
  imageTooLarge(0x09),
  badOffset(0x0a),
  dataMismatch(0x0b),
  crcMismatch(0x0c),
  invalidJpeg(0x0d),
  unsupportedFormat(0x0e),
  notFound(0x0f),
  internalError(0x10),
  receiving(0x11),
  verifying(0x12),
  stored(0x13),
  displaying(0x14),
  completed(0x15),
  unknown(0xff);

  const PaperMonoStatus(this.code);
  final int code;

  static PaperMonoStatus fromCode(int code) {
    return values.firstWhere(
      (status) => status.code == code,
      orElse: () => unknown,
    );
  }
}

class PaperMonoResponse {
  const PaperMonoResponse({
    required this.command,
    required this.status,
    required this.transferId,
    required this.nextExpectedOffset,
    required this.extra,
  });

  final PaperMonoCommand command;
  final PaperMonoStatus status;
  final int transferId;
  final int nextExpectedOffset;
  final Uint8List extra;
}
