package io.github.corvelis.paper_mono_image_sender

import java.nio.ByteBuffer
import java.nio.ByteOrder

internal object PaperMonoProtocol {
    private const val MAGIC_0: Byte = 0x50
    private const val MAGIC_1: Byte = 0x4d
    private const val VERSION: Byte = 0x01
    const val MAX_ACCEPTED_RF_FRAME_BYTES = 255
    const val MAX_PROTOCOL_COMMAND_BYTES = 253
    const val MAX_DATA_PAYLOAD_BYTES = 240
    const val DATA_HEADER_BYTES = 13
    const val MAX_IMAGE_BYTES = 262144
    private const val RESPONSE_ENVELOPE_BYTES = 13

    fun hello(): ByteArray = prefix(PaperMonoCommand.HELLO)

    fun begin(transfer: PendingTransfer): ByteArray = buffer(23).apply {
        putPrefix(PaperMonoCommand.BEGIN)
        putInt(transfer.transferId.toInt())
        put(1.toByte()) // BEGIN(REPLACE); matching IDs still resume
        put(transfer.mode.toByte())
        put(1.toByte()) // Baseline JPEG, 3 components
        putShort(transfer.width.toShort())
        putShort(transfer.height.toShort())
        putInt(transfer.bytes.size)
        putInt(transfer.crc32.toInt())
    }.array()

    fun data(transferId: Long, offset: Int, payload: ByteArray): ByteArray {
        require(payload.size <= MAX_DATA_PAYLOAD_BYTES)
        return buffer(DATA_HEADER_BYTES + payload.size).apply {
            putPrefix(PaperMonoCommand.DATA)
            putInt(transferId.toInt())
            putInt(offset)
            put(payload.size.toByte())
            put(payload)
        }.array()
    }

    fun status(transferId: Long): ByteArray = buffer(8).apply {
        putPrefix(PaperMonoCommand.STATUS)
        putInt(transferId.toInt())
    }.array()

    fun commit(transferId: Long, crc32: Long): ByteArray = buffer(12).apply {
        putPrefix(PaperMonoCommand.COMMIT)
        putInt(transferId.toInt())
        putInt(crc32.toInt())
    }.array()

    fun abort(transferId: Long): ByteArray = buffer(8).apply {
        putPrefix(PaperMonoCommand.ABORT)
        putInt(transferId.toInt())
    }.array()

    fun setTime(unixTimeSeconds: Long, utcOffsetMinutes: Int): ByteArray = buffer(15).apply {
        putPrefix(PaperMonoCommand.SET_TIME)
        putLong(unixTimeSeconds)
        putShort(utcOffsetMinutes.toShort())
        put(0.toByte()) // Reserved flags.
    }.array()

    fun parseResponse(bytes: ByteArray, expected: PaperMonoCommand): PaperMonoResponse {
        if (bytes.size < RESPONSE_ENVELOPE_BYTES) {
            throw ProtocolException("SHORT_RESPONSE", "PaperMonoの応答が13バイト未満です。")
        }
        val data = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        if (data.get() != MAGIC_0 || data.get() != MAGIC_1) {
            throw ProtocolException("BAD_MAGIC", "PaperMono応答のMagicが一致しません。")
        }
        if (data.get() != VERSION) {
            throw ProtocolException("BAD_VERSION", "PaperMonoのプロトコルバージョンが一致しません。")
        }
        val command = data.get().toInt() and 0xff
        if (command != (expected.code or 0x80)) {
            throw ProtocolException("BAD_COMMAND", "PaperMonoの応答コマンドが一致しません。")
        }
        val status = PaperMonoStatus.fromCode(data.get().toInt() and 0xff)
        val transferId = data.int.toLong() and 0xffffffffL
        val nextOffset = data.int.toLong() and 0xffffffffL
        return PaperMonoResponse(status, transferId, nextOffset, bytes.copyOfRange(13, bytes.size))
    }

    fun parseHello(response: PaperMonoResponse): HelloCapabilities {
        if (response.extra.size < 11) {
            throw ProtocolException("SHORT_HELLO", "PaperMonoのHELLO応答が短すぎます。")
        }
        val data = ByteBuffer.wrap(response.extra).order(ByteOrder.LITTLE_ENDIAN)
        val maxRfFrame = data.short.toInt() and 0xffff
        val maxProtocol = data.short.toInt() and 0xffff
        val maxPayload = data.short.toInt() and 0xffff
        val maxImage = data.int.toLong() and 0xffffffffL
        val capabilities = data.get().toInt() and 0xff
        if (
            maxRfFrame > MAX_ACCEPTED_RF_FRAME_BYTES ||
            maxProtocol > MAX_PROTOCOL_COMMAND_BYTES ||
            maxPayload > MAX_DATA_PAYLOAD_BYTES ||
            maxImage > MAX_IMAGE_BYTES.toLong()
        ) {
            throw ProtocolException("UNSAFE_HELLO", "PaperMonoがv1の安全上限を超える能力値を返しました。")
        }
        return HelloCapabilities(
            maxRfFrame,
            maxProtocol,
            maxPayload,
            maxImage,
            capabilities and 0x01 != 0,
            capabilities and 0x02 != 0,
        )
    }

    private fun prefix(command: PaperMonoCommand): ByteArray = buffer(4).apply {
        putPrefix(command)
    }.array()

    private fun ByteBuffer.putPrefix(command: PaperMonoCommand) {
        put(MAGIC_0)
        put(MAGIC_1)
        put(VERSION)
        put(command.code.toByte())
    }

    private fun buffer(size: Int): ByteBuffer = ByteBuffer.allocate(size).order(ByteOrder.LITTLE_ENDIAN)
}

internal enum class PaperMonoCommand(val code: Int) {
    HELLO(0x01), BEGIN(0x02), DATA(0x03), STATUS(0x04), COMMIT(0x05), ABORT(0x06),
    SET_TIME(0x07)
}

internal enum class PaperMonoStatus(val code: Int) {
    OK(0x00), ACCEPTED(0x01), BUSY(0x02), CONFLICT(0x03), BAD_MAGIC(0x04),
    UNSUPPORTED_VERSION(0x05), UNKNOWN_COMMAND(0x06), INVALID_LENGTH(0x07),
    PAYLOAD_TOO_LARGE(0x08), IMAGE_TOO_LARGE(0x09), BAD_OFFSET(0x0a),
    DATA_MISMATCH(0x0b), CRC_MISMATCH(0x0c), INVALID_JPEG(0x0d),
    UNSUPPORTED_FORMAT(0x0e), NOT_FOUND(0x0f), INTERNAL_ERROR(0x10),
    RECEIVING(0x11), VERIFYING(0x12), STORED(0x13), DISPLAYING(0x14),
    COMPLETED(0x15), UNKNOWN(0xff);

    companion object {
        fun fromCode(code: Int): PaperMonoStatus = entries.firstOrNull { it.code == code } ?: UNKNOWN
    }
}

internal data class PaperMonoResponse(
    val status: PaperMonoStatus,
    val transferId: Long,
    val nextExpectedOffset: Long,
    val extra: ByteArray,
)

internal data class HelloCapabilities(
    val maxAcceptedRfFrameBytes: Int,
    val maxProtocolCommandBytes: Int,
    val maxDataPayloadBytes: Int,
    val maxImageBytes: Long,
    val supportsBaselineJpeg3Component: Boolean,
    val supportsTimeSync: Boolean,
)
