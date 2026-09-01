package io.github.corvelis.paper_mono_image_sender

import android.nfc.NfcAdapter
import android.nfc.Tag
import android.nfc.TagLostException
import android.nfc.tech.NfcA
import android.os.Bundle
import android.util.Log
import android.view.WindowManager
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import java.io.IOException
import java.util.concurrent.atomic.AtomicBoolean

class MainActivity : FlutterActivity(), NfcAdapter.ReaderCallback, EventChannel.StreamHandler {
    companion object {
        private const val TAG = "PaperMonoNfc"
        private const val METHOD_CHANNEL = "io.github.corvelis.paper_mono_image_sender/methods"
        private const val EVENT_CHANNEL = "io.github.corvelis.paper_mono_image_sender/events"
        private const val TRANSCEIVE_TIMEOUT_MS = 1000
        private const val COMMIT_POLL_MS = 200L
        private const val COMMIT_TRACK_MS = 30_000L
        private const val DATA_INTER_COMMAND_MS = 8L
        private const val INVALID_RESPONSE_RETRY_MS = 10L
        private const val EXCHANGE_ATTEMPTS = 3
        private const val PREFERRED_DATA_PAYLOAD_BYTES = 128
        private const val FALLBACK_DATA_PAYLOAD_BYTES = 64
    }

    @Volatile private var pendingTransfer: PendingTransfer? = null
    @Volatile private var eventSink: EventChannel.EventSink? = null
    private val transferRunning = AtomicBoolean(false)
    private val cancelled = AtomicBoolean(false)
    private var nfcAdapter: NfcAdapter? = null
    private var readerModeEnabled = false
    @Volatile private var dataPayloadLimit = PREFERRED_DATA_PAYLOAD_BYTES

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        nfcAdapter = NfcAdapter.getDefaultAdapter(this)
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, METHOD_CHANNEL)
            .setMethodCallHandler(::handleMethodCall)
        EventChannel(flutterEngine.dartExecutor.binaryMessenger, EVENT_CHANNEL)
            .setStreamHandler(this)
    }

    override fun onResume() {
        super.onResume()
        Log.i(TAG, "onResume pending=${pendingTransfer != null}")
        if (pendingTransfer != null) enableReaderMode()
    }

    override fun onPause() {
        Log.i(TAG, "onPause pending=${pendingTransfer != null}; disabling reader mode")
        disableReaderMode()
        super.onPause()
    }

    override fun onListen(arguments: Any?, events: EventChannel.EventSink?) {
        eventSink = events
    }

    override fun onCancel(arguments: Any?) {
        eventSink = null
    }

    private fun handleMethodCall(call: MethodCall, result: MethodChannel.Result) {
        when (call.method) {
            "isAvailable" -> result.success(nfcAdapter?.isEnabled == true)
            "startTransfer" -> startTransfer(call, result)
            "syncClock" -> syncClock(call, result)
            "cancelTransfer" -> {
                cancelled.set(true)
                pendingTransfer = null
                disableReaderMode()
                setTransferKeepsScreenOn(false)
                emit("idle", message = "送信を中止しました。")
                result.success(null)
            }
            else -> result.notImplemented()
        }
    }

    private fun startTransfer(call: MethodCall, result: MethodChannel.Result) {
        val adapter = nfcAdapter
        if (adapter == null) {
            result.error("NFC_UNAVAILABLE", "このAndroid端末はNFCに対応していません。", null)
            return
        }
        if (!adapter.isEnabled) {
            result.error("NFC_DISABLED", "AndroidのNFCを有効にしてください。", null)
            return
        }
        val bytes = call.argument<ByteArray>("bytes")
        val mode = call.numberArgument("mode")?.toInt()
        val width = call.numberArgument("width")?.toInt()
        val height = call.numberArgument("height")?.toInt()
        val crc32 = call.numberArgument("crc32")?.toLong()?.and(0xffffffffL)
        val transferId = call.numberArgument("transferId")?.toLong()?.and(0xffffffffL)
        val unixTimeSeconds = call.numberArgument("unixTimeSeconds")?.toLong()
        val utcOffsetMinutes = call.numberArgument("utcOffsetMinutes")?.toInt()
        if (bytes == null || mode == null || width == null || height == null || crc32 == null ||
            transferId == null || unixTimeSeconds == null || utcOffsetMinutes == null
        ) {
            result.error("INVALID_ARGUMENTS", "送信パラメータが不足しています。", null)
            return
        }
        if (bytes.isEmpty() || bytes.size > PaperMonoProtocol.MAX_IMAGE_BYTES || transferId == 0L) {
            result.error("INVALID_IMAGE", "画像サイズまたは転送IDが不正です。", null)
            return
        }
        if (!validClock(unixTimeSeconds, utcOffsetMinutes)) {
            result.error("INVALID_TIME", "スマートフォンの時刻またはタイムゾーンが不正です。", null)
            return
        }
        pendingTransfer = PendingTransfer(
            bytes, mode, width, height, crc32, transferId,
            unixTimeSeconds, utcOffsetMinutes,
        )
        dataPayloadLimit = PREFERRED_DATA_PAYLOAD_BYTES
        cancelled.set(false)
        setTransferKeepsScreenOn(true)
        Log.i(TAG, "startTransfer id=$transferId bytes=${bytes.size} mode=$mode ${width}x$height")
        emit("waitingForTag", 0, bytes.size, 0, "PaperMonoにスマートフォンを当ててください。")
        enableReaderMode()
        result.success(null)
    }

    private fun syncClock(call: MethodCall, result: MethodChannel.Result) {
        val adapter = nfcAdapter
        if (adapter == null) {
            result.error("NFC_UNAVAILABLE", "このAndroid端末はNFCに対応していません。", null)
            return
        }
        if (!adapter.isEnabled) {
            result.error("NFC_DISABLED", "AndroidのNFCを有効にしてください。", null)
            return
        }
        val unixTimeSeconds = call.numberArgument("unixTimeSeconds")?.toLong()
        val utcOffsetMinutes = call.numberArgument("utcOffsetMinutes")?.toInt()
        if (unixTimeSeconds == null || utcOffsetMinutes == null ||
            !validClock(unixTimeSeconds, utcOffsetMinutes)
        ) {
            result.error("INVALID_TIME", "スマートフォンの時刻またはタイムゾーンが不正です。", null)
            return
        }
        pendingTransfer = PendingTransfer(
            ByteArray(0), 0, 0, 0, 0, 0,
            unixTimeSeconds, utcOffsetMinutes, clockOnly = true,
        )
        cancelled.set(false)
        setTransferKeepsScreenOn(true)
        emit("waitingForTag", message = "Paper Monoにスマートフォンを当ててください。")
        enableReaderMode()
        result.success(null)
    }

    private fun validClock(unixTimeSeconds: Long, utcOffsetMinutes: Int): Boolean =
        unixTimeSeconds in 1_672_531_200L until 4_102_444_800L && utcOffsetMinutes in -840..840

    private fun MethodCall.numberArgument(name: String): Number? = argument<Number>(name)

    private fun enableReaderMode() {
        val adapter = nfcAdapter ?: return
        if (readerModeEnabled || pendingTransfer == null || isFinishing) {
            Log.i(
                TAG,
                "enableReaderMode skipped enabled=$readerModeEnabled pending=${pendingTransfer != null} finishing=$isFinishing",
            )
            return
        }
        adapter.enableReaderMode(
            this,
            this,
            NfcAdapter.FLAG_READER_NFC_A or
                NfcAdapter.FLAG_READER_SKIP_NDEF_CHECK or
                NfcAdapter.FLAG_READER_NO_PLATFORM_SOUNDS,
            Bundle(),
        )
        readerModeEnabled = true
        Log.i(TAG, "Reader Mode enabled flags=NFC_A|SKIP_NDEF_CHECK|NO_PLATFORM_SOUNDS")
    }

    private fun disableReaderMode() {
        if (!readerModeEnabled) return
        nfcAdapter?.disableReaderMode(this)
        readerModeEnabled = false
        Log.i(TAG, "Reader Mode disabled")
    }

    private fun setTransferKeepsScreenOn(enabled: Boolean) {
        runOnUiThread {
            if (enabled) {
                window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            } else {
                window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            }
            Log.i(TAG, "keepScreenOn=$enabled")
        }
    }

    override fun onTagDiscovered(tag: Tag) {
        val transfer = pendingTransfer ?: return
        if (!transferRunning.compareAndSet(false, true)) return
        Log.i(TAG, "tag discovered tech=${tag.techList.joinToString()} idBytes=${tag.id?.size ?: 0}")
        val nfcA = NfcA.get(tag)
        if (nfcA == null) {
            Log.w(TAG, "discovered tag does not expose NfcA")
            emit("recoverableError", 0, transfer.bytes.size, 0, "NFC-Aタグではありません。")
            transferRunning.set(false)
            return
        }

        try {
            nfcA.connect()
            nfcA.timeout = TRANSCEIVE_TIMEOUT_MS
            Log.i(TAG, "NfcA connected maxTransceiveLength=${nfcA.maxTransceiveLength}")
            if (nfcA.maxTransceiveLength < 61) {
                throw ProtocolException(
                    "TRANSCEIVE_LIMIT_TOO_SMALL",
                    "このAndroid端末のNFCコマンド上限が61バイト未満です。",
                )
            }
            emit("connected", 0, transfer.bytes.size, 0, "PaperMonoに接続しました。")
            performTransfer(nfcA, transfer)
        } catch (error: TagLostException) {
            Log.w(TAG, "tag lost", error)
            emitRecoverable(transfer, "接続が切れました。もう一度PaperMonoに当ててください。")
        } catch (error: IOException) {
            Log.w(TAG, "NFC I/O error", error)
            emitRecoverable(transfer, error.message ?: "NFC通信に失敗しました。")
        } catch (error: ProtocolException) {
            Log.w(TAG, "protocol error code=${error.code}", error)
            if (error.code == "CANCELLED") {
                tryAbort(nfcA, transfer)
            } else if (error.code == "TRANSFER_ID_MISMATCH") {
                emitRecoverable(transfer, "転送応答を再同期します。もう一度PaperMonoへ当ててください。")
            } else {
                pendingTransfer = null
                setTransferKeepsScreenOn(false)
                emit("failed", 0, transfer.bytes.size, 0, error.message, error.code)
                runOnUiThread { disableReaderMode() }
            }
        } catch (error: Throwable) {
            Log.e(TAG, "unexpected NFC transfer error", error)
            pendingTransfer = null
            setTransferKeepsScreenOn(false)
            emit("failed", 0, transfer.bytes.size, 0, error.message ?: error.toString(), "INTERNAL_ERROR")
            runOnUiThread { disableReaderMode() }
        } finally {
            try {
                nfcA.close()
            } catch (_: IOException) {
            }
            transferRunning.set(false)
        }
    }

    private fun performTransfer(nfcA: NfcA, transfer: PendingTransfer) {
        ensureNotCancelled()
        Log.i(TAG, "HELLO send id=${transfer.transferId}")
        val hello = exchange(nfcA, PaperMonoProtocol.hello(), PaperMonoCommand.HELLO)
        Log.i(TAG, "HELLO response status=${hello.status}")
        requireStatus(hello, setOf(PaperMonoStatus.OK))
        val capabilities = PaperMonoProtocol.parseHello(hello)
        if (!capabilities.supportsTimeSync) {
            throw ProtocolException("TIME_SYNC_UNSUPPORTED", "Paper Monoが時刻同期に対応していません。")
        }
        emit("clockSyncing", totalBytes = transfer.bytes.size, message = "時刻を同期しています。")
        val timeResponse = exchange(
            nfcA,
            PaperMonoProtocol.setTime(transfer.unixTimeSeconds, transfer.utcOffsetMinutes),
            PaperMonoCommand.SET_TIME,
        )
        requireStatus(timeResponse, setOf(PaperMonoStatus.OK))
        if (transfer.clockOnly) {
            finishClockSync()
            return
        }
        if (
            capabilities.maxAcceptedRfFrameBytes < 63 ||
            capabilities.maxProtocolCommandBytes < 61 ||
            capabilities.maxDataPayloadBytes < 48 ||
            capabilities.maxImageBytes < transfer.bytes.size.toLong() ||
            !capabilities.supportsBaselineJpeg3Component
        ) {
            throw ProtocolException("INCOMPATIBLE_LIMITS", "PaperMonoが画像転送に必要なv1能力を提供していません。")
        }
        val dataPayloadBytes = minOf(
            capabilities.maxDataPayloadBytes,
            PaperMonoProtocol.MAX_DATA_PAYLOAD_BYTES,
            dataPayloadLimit,
            nfcA.maxTransceiveLength - PaperMonoProtocol.DATA_HEADER_BYTES,
        )
        Log.i(TAG, "DATA payload bytes=$dataPayloadBytes")
        if (dataPayloadBytes < 48) {
            throw ProtocolException(
                "TRANSCEIVE_LIMIT_TOO_SMALL",
                "このAndroid端末では48バイト以上のDATAペイロードを送信できません。",
            )
        }

        ensureNotCancelled()
        val begin = exchange(nfcA, PaperMonoProtocol.begin(transfer), PaperMonoCommand.BEGIN)
        requireTransferId(begin, transfer)
        requireStatus(
            begin,
            setOf(
                PaperMonoStatus.OK,
                PaperMonoStatus.ACCEPTED,
                PaperMonoStatus.RECEIVING,
                PaperMonoStatus.STORED,
                PaperMonoStatus.DISPLAYING,
                PaperMonoStatus.COMPLETED,
            ),
        )
        var offset = begin.nextExpectedOffset.toInt()
        if (offset !in 0..transfer.bytes.size) {
            throw ProtocolException("INVALID_OFFSET", "PaperMonoが不正な再開位置を返しました。")
        }

        var stalls = 0
        while (offset < transfer.bytes.size) {
            ensureNotCancelled()
            val end = minOf(offset + dataPayloadBytes, transfer.bytes.size)
            val payload = transfer.bytes.copyOfRange(offset, end)
            val response = try {
                exchange(
                    nfcA,
                    PaperMonoProtocol.data(transfer.transferId, offset, payload),
                    PaperMonoCommand.DATA,
                )
            } catch (error: IOException) {
                if (dataPayloadLimit > FALLBACK_DATA_PAYLOAD_BYTES) {
                    dataPayloadLimit = FALLBACK_DATA_PAYLOAD_BYTES
                }
                throw error
            }
            requireTransferId(response, transfer)
            requireStatus(
                response,
                setOf(PaperMonoStatus.OK, PaperMonoStatus.RECEIVING, PaperMonoStatus.BAD_OFFSET),
            )
            val next = response.nextExpectedOffset.toInt()
            if (next !in 0..transfer.bytes.size) {
                throw ProtocolException("INVALID_OFFSET", "PaperMonoが不正な受信位置を返しました。")
            }
            stalls = if (next == offset) stalls + 1 else 0
            if (stalls >= 3) {
                throw ProtocolException("TRANSFER_STALLED", "PaperMonoの受信位置が進みません。")
            }
            offset = next
            emit("receiving", offset, transfer.bytes.size, offset)
            if (offset < transfer.bytes.size) Thread.sleep(DATA_INTER_COMMAND_MS)
        }

        ensureNotCancelled()
        val commit = exchange(
            nfcA,
            PaperMonoProtocol.commit(transfer.transferId, transfer.crc32),
            PaperMonoCommand.COMMIT,
        )
        requireTransferId(commit, transfer)
        handleCommitStatus(commit, transfer)
        if (commit.status in setOf(PaperMonoStatus.STORED, PaperMonoStatus.DISPLAYING, PaperMonoStatus.COMPLETED)) {
            // PaperMono may stop RF immediately after durable storage while
            // refreshing e-paper. Do not misclassify that expected field loss
            // as a recoverable transfer failure.
            finishTransfer(transfer)
            return
        }

        val deadline = System.currentTimeMillis() + COMMIT_TRACK_MS
        var latest = commit
        while (
            latest.status !in setOf(PaperMonoStatus.STORED, PaperMonoStatus.DISPLAYING, PaperMonoStatus.COMPLETED) &&
            System.currentTimeMillis() < deadline
        ) {
            ensureNotCancelled()
            Thread.sleep(COMMIT_POLL_MS)
            latest = exchange(
                nfcA,
                PaperMonoProtocol.status(transfer.transferId),
                PaperMonoCommand.STATUS,
            )
            requireTransferId(latest, transfer)
            handleCommitStatus(latest, transfer)
        }
        if (latest.status in setOf(PaperMonoStatus.STORED, PaperMonoStatus.DISPLAYING, PaperMonoStatus.COMPLETED)) {
            finishTransfer(transfer)
            return
        }
        throw ProtocolException("COMMIT_TIMEOUT", "PaperMonoの保存確認がタイムアウトしました。")
    }

    private fun handleCommitStatus(response: PaperMonoResponse, transfer: PendingTransfer) {
        when (response.status) {
            PaperMonoStatus.OK, PaperMonoStatus.ACCEPTED, PaperMonoStatus.VERIFYING ->
                emit("verifying", transfer.bytes.size, transfer.bytes.size, response.nextExpectedOffset.toInt())
            PaperMonoStatus.STORED ->
                emit("stored", transfer.bytes.size, transfer.bytes.size, response.nextExpectedOffset.toInt())
            PaperMonoStatus.DISPLAYING ->
                emit("displaying", transfer.bytes.size, transfer.bytes.size, response.nextExpectedOffset.toInt())
            PaperMonoStatus.COMPLETED -> Unit
            PaperMonoStatus.CRC_MISMATCH ->
                throw ProtocolException("CRC_MISMATCH", "PaperMonoで画像CRCが一致しませんでした。")
            PaperMonoStatus.INVALID_JPEG ->
                throw ProtocolException("INVALID_JPEG", "PaperMonoがJPEGを受け付けませんでした。")
            else -> throw ProtocolException(
                "COMMIT_REJECTED",
                "PaperMonoがCOMMITを拒否しました: ${response.status.name}",
            )
        }
    }

    private fun finishTransfer(transfer: PendingTransfer, emitCompleted: Boolean = true) {
        pendingTransfer = null
        setTransferKeepsScreenOn(false)
        Log.i(TAG, "transfer completed id=${transfer.transferId} bytes=${transfer.bytes.size}")
        if (emitCompleted) emit("completed", transfer.bytes.size, transfer.bytes.size, transfer.bytes.size)
        runOnUiThread { disableReaderMode() }
    }

    private fun finishClockSync() {
        pendingTransfer = null
        setTransferKeepsScreenOn(false)
        emit("clockSynced", message = "Paper Monoの時刻を同期しました。")
        runOnUiThread { disableReaderMode() }
    }

    private fun exchange(nfcA: NfcA, command: ByteArray, expected: PaperMonoCommand): PaperMonoResponse {
        var lastError: IOException? = null
        repeat(EXCHANGE_ATTEMPTS) { attempt ->
            try {
                val rawResponse = nfcA.transceive(command)
                try {
                    return PaperMonoProtocol.parseResponse(rawResponse, expected)
                } catch (error: ProtocolException) {
                    val hex = rawResponse.joinToString(separator = "") {
                        "%02X".format(it.toInt() and 0xff)
                    }
                    Log.w(
                        TAG,
                        "invalid response command=$expected bytes=${rawResponse.size} " +
                            "data=$hex attempt=${attempt + 1}/$EXCHANGE_ATTEMPTS code=${error.code}",
                    )
                    if (attempt + 1 >= EXCHANGE_ATTEMPTS) {
                        // A malformed/truncated RF response is a transport
                        // failure. All v1 commands are idempotent, so retrying
                        // is safe; after the final attempt keep the transfer
                        // pending and let the next tag discovery resume it.
                        throw IOException(error.message ?: "Invalid PaperMono response", error)
                    }
                    Thread.sleep(INVALID_RESPONSE_RETRY_MS)
                }
            } catch (error: TagLostException) {
                throw error
            } catch (error: IOException) {
                lastError = error
                if (attempt + 1 < EXCHANGE_ATTEMPTS) {
                    Thread.sleep(INVALID_RESPONSE_RETRY_MS)
                }
            }
        }
        throw lastError ?: IOException("NFC transceive failed")
    }

    private fun requireTransferId(response: PaperMonoResponse, transfer: PendingTransfer) {
        if (response.transferId != transfer.transferId) {
            throw ProtocolException("TRANSFER_ID_MISMATCH", "PaperMonoの転送IDが一致しません。")
        }
    }

    private fun requireStatus(response: PaperMonoResponse, accepted: Set<PaperMonoStatus>) {
        if (response.status !in accepted) {
            throw ProtocolException(response.status.name, "PaperMonoがコマンドを拒否しました: ${response.status.name}")
        }
    }

    private fun ensureNotCancelled() {
        if (cancelled.get()) throw ProtocolException("CANCELLED", "送信を中止しました。")
    }

    private fun tryAbort(nfcA: NfcA, transfer: PendingTransfer) {
        if (transfer.clockOnly) return
        try {
            if (nfcA.isConnected) {
                nfcA.transceive(PaperMonoProtocol.abort(transfer.transferId))
            }
        } catch (_: IOException) {
            // Cancellation still succeeds locally if the tag has already left.
        }
    }

    private fun emitRecoverable(transfer: PendingTransfer, message: String) {
        emit("recoverableError", 0, transfer.bytes.size, 0, message, "TAG_LOST")
        emit("waitingForTag", 0, transfer.bytes.size, 0, "PaperMonoへもう一度当てると途中から再開します。")
    }

    private fun emit(
        phase: String,
        bytesSent: Int = 0,
        totalBytes: Int = pendingTransfer?.bytes?.size ?: 0,
        nextExpectedOffset: Int = 0,
        message: String? = null,
        errorCode: String? = null,
    ) {
        val event = hashMapOf<String, Any>(
            "phase" to phase,
            "bytesSent" to bytesSent,
            "totalBytes" to totalBytes,
            "nextExpectedOffset" to nextExpectedOffset,
        )
        if (message != null) event["message"] = message
        if (errorCode != null) event["errorCode"] = errorCode
        runOnUiThread { eventSink?.success(event) }
    }
}

internal data class PendingTransfer(
    val bytes: ByteArray,
    val mode: Int,
    val width: Int,
    val height: Int,
    val crc32: Long,
    val transferId: Long,
    val unixTimeSeconds: Long,
    val utcOffsetMinutes: Int,
    val clockOnly: Boolean = false,
)

internal class ProtocolException(val code: String, message: String) : Exception(message)
