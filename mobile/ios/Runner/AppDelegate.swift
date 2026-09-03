import CoreNFC
import Flutter
import UIKit

@main
@objc class AppDelegate: FlutterAppDelegate {
  private var nfcTransferManager: NfcTransferManager?

  override func application(
    _ application: UIApplication,
    didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?
  ) -> Bool {
    GeneratedPluginRegistrant.register(with: self)
    if let controller = window?.rootViewController as? FlutterViewController {
      let manager = NfcTransferManager(messenger: controller.binaryMessenger)
      nfcTransferManager = manager
    }
    return super.application(application, didFinishLaunchingWithOptions: launchOptions)
  }
}

@available(iOS 13.0, *)
private final class NfcTransferManager: NSObject,
  NFCTagReaderSessionDelegate,
  FlutterStreamHandler
{
  private static let methodChannelName = "io.github.corvelis.paper_mono_image_sender/methods"
  private static let eventChannelName = "io.github.corvelis.paper_mono_image_sender/events"
  private static let commitPollDelay = 0.2
  private static let commitTrackSeconds = 30.0
  // The PaperMono target is serviced over 400 kHz I2C. Back-to-back Core NFC
  // transceives can re-arm the reader before the ST25R3916 listener has fully
  // returned to RX, which showed up as repeatable connection loss after a few
  // kilobytes. A short pacing gap is still much smaller than the RF exchange
  // time but gives the emulated target a deterministic receive window.
  // Production prefers 128-byte payloads. The 240-byte protocol maximum makes
  // a 255-byte RF frame and has shown CRC/parity errors at the FIFO boundary.
  private static let dataInterCommandDelay = 0.008
  private static let preferredDataPayloadBytes = 128
  private static let fallbackDataPayloadBytes = 64
  private static let automaticRestartLimit = 4
  private static let automaticRestartDelay = 1.5

  private let nfcQueue = DispatchQueue(label: "io.github.corvelis.paper-mono-nfc-transfer")
  private var eventSink: FlutterEventSink?
  private var session: NFCTagReaderSession?
  private var activeTag: NFCMiFareTag?
  private var pendingTransfer: PendingTransfer?
  private var cancelled = false
  private var sessionRestartPending = false
  private var sessionRestartAttempts = 0
  private var communicationRecoveryPending = false
  private var lastSystemProgressPercent = -1
  private var languageCode = "ja"
  // Swift does not allow covariant `Self` in a stored-property initializer.
  private var dataPayloadLimit = 128
  // Core NFC releases the reader hardware asynchronously. Keep the session
  // owned until didInvalidateWithError arrives; otherwise a second transfer
  // can start while iOS still owns the previous RF session and fail with
  // resource-unavailable errors (typically NFCError 202/203).
  private var terminalAction: SessionTerminalAction?

  private func updateLanguage(from arguments: [String: Any]?) {
    languageCode = arguments?["language"] as? String == "en" ? "en" : "ja"
  }

  private func text(_ japanese: String, _ english: String) -> String {
    languageCode == "en" ? english : japanese
  }

  private func localizedError(code: String, fallback: String) -> String {
    switch code {
    case "TIME_SYNC_UNSUPPORTED":
      return text("Paper Monoが時刻同期に対応していません。", "Paper Mono does not support clock sync.")
    case "FULLSCREEN_UNSUPPORTED":
      return text("Paper Monoが全画面画像に対応していません。", "Paper Mono does not support full-screen images.")
    case "INVALID_OFFSET", "TRANSFER_STALLED":
      return text("Paper Monoから不正な受信位置が返されました。", "Paper Mono returned an invalid transfer position.")
    case "TRANSFER_ID_MISMATCH":
      return text("Paper Monoの転送IDが一致しません。", "The Paper Mono transfer ID does not match.")
    case "COMMIT_TIMEOUT":
      return text("Paper Monoの保存確認がタイムアウトしました。", "Timed out while waiting for Paper Mono to store the image.")
    case "CRC_MISMATCH":
      return text("Paper Monoで画像CRCが一致しませんでした。", "The image CRC did not match on Paper Mono.")
    case "INVALID_JPEG":
      return text("Paper MonoがJPEGを受け付けませんでした。", "Paper Mono rejected the JPEG.")
    default:
      return languageCode == "en" ? "The NFC operation failed (\(code))." : fallback
    }
  }

  init(messenger: FlutterBinaryMessenger) {
    super.init()
    FlutterMethodChannel(name: Self.methodChannelName, binaryMessenger: messenger)
      .setMethodCallHandler { [weak self] call, result in
        self?.handle(call, result: result)
      }
    FlutterEventChannel(name: Self.eventChannelName, binaryMessenger: messenger)
      .setStreamHandler(self)
  }

  func onListen(
    withArguments arguments: Any?,
    eventSink events: @escaping FlutterEventSink
  ) -> FlutterError? {
    eventSink = events
    return nil
  }

  func onCancel(withArguments arguments: Any?) -> FlutterError? {
    eventSink = nil
    return nil
  }

  private func handle(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
    switch call.method {
    case "isAvailable":
      result(NFCReaderSession.readingAvailable)
    case "startTransfer":
      startTransfer(call, result: result)
    case "syncClock":
      syncClock(call, result: result)
    case "cancelTransfer":
      cancelled = true
      if let transfer = pendingTransfer,
         !transfer.clockOnly,
         let tag = activeTag,
         tag.isAvailable
      {
        tag.sendMiFareCommand(
          commandPacket: PaperMonoProtocol.abort(transfer.transferId)
        ) { [weak self] _, _ in
          self?.finishCancellation()
        }
      } else {
        finishCancellation()
      }
      result(nil)
    default:
      result(FlutterMethodNotImplemented)
    }
  }

  private func startTransfer(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
    let rawArguments = call.arguments as? [String: Any]
    updateLanguage(from: rawArguments)
    guard NFCReaderSession.readingAvailable else {
      result(FlutterError(
        code: "NFC_UNAVAILABLE",
        message: text("このiPhoneではNFCタグ読み取りを利用できません。", "NFC tag reading is unavailable on this iPhone."),
        details: nil
      ))
      return
    }
    guard
      let arguments = rawArguments,
      let typedBytes = arguments["bytes"] as? FlutterStandardTypedData,
      let mode = (arguments["mode"] as? NSNumber)?.uint8Value,
      let width = (arguments["width"] as? NSNumber)?.uint16Value,
      let height = (arguments["height"] as? NSNumber)?.uint16Value,
      let crc32 = (arguments["crc32"] as? NSNumber)?.uint32Value,
      let transferId = (arguments["transferId"] as? NSNumber)?.uint32Value
    else {
      result(FlutterError(
        code: "INVALID_ARGUMENTS",
        message: text("送信パラメータが不足しています。", "Transfer parameters are missing."),
        details: nil
      ))
      return
    }
    let bytes = typedBytes.data
    guard !bytes.isEmpty,
          bytes.count <= PaperMonoProtocol.maxImageBytes,
          transferId != 0
    else {
      result(FlutterError(
        code: "INVALID_IMAGE",
        message: text("画像サイズまたは転送IDが不正です。", "The image size or transfer ID is invalid."),
        details: nil
      ))
      return
    }

    // A Core NFC reader session must not be replaced while it is becoming
    // active, polling, or transferring. Repeated taps used to invalidate the
    // current session and immediately create another one, which made iOS
    // report codes 202/203 (unexpected invalidation/resource unavailable).
    if session != nil || sessionRestartPending {
      if pendingTransfer?.transferId == transferId {
        print("[nfc.ios] ignored duplicate start id=\(transferId): session active or restarting")
        result(nil)
      } else {
        result(FlutterError(
          code: "TRANSFER_IN_PROGRESS",
          message: text("別のNFC送信が進行中です。先に中止してください。", "Another NFC transfer is active. Cancel it first."),
          details: nil
        ))
      }
      return
    }

    pendingTransfer = PendingTransfer(
      bytes: bytes,
      mode: mode,
      width: width,
      height: height,
      crc32: crc32,
      transferId: transferId,
      unixTimeSeconds: 0,
      utcOffsetMinutes: 0,
      clockOnly: false
    )
    print("[nfc.ios] start transfer id=\(transferId) bytes=\(bytes.count)")
    cancelled = false
    communicationRecoveryPending = false
    sessionRestartAttempts = 0
    lastSystemProgressPercent = -1
    dataPayloadLimit = Self.preferredDataPayloadBytes
    terminalAction = nil
    guard beginReaderSession(
      totalBytes: bytes.count,
      message: text("Paper MonoにiPhoneを当ててください。", "Hold your iPhone near Paper Mono.")
    ) else {
      pendingTransfer = nil
      result(FlutterError(
        code: "NFC_SESSION_UNAVAILABLE",
        message: text("NFC読み取りセッションを開始できませんでした。", "The NFC reader session could not be started."),
        details: nil
      ))
      return
    }
    result(nil)
  }

  private func syncClock(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
    let rawArguments = call.arguments as? [String: Any]
    updateLanguage(from: rawArguments)
    guard NFCReaderSession.readingAvailable else {
      result(FlutterError(
        code: "NFC_UNAVAILABLE",
        message: text("このiPhoneではNFCタグ読み取りを利用できません。", "NFC tag reading is unavailable on this iPhone."),
        details: nil
      ))
      return
    }
    guard
      let arguments = rawArguments,
      let unixTimeSeconds = (arguments["unixTimeSeconds"] as? NSNumber)?.uint64Value,
      let utcOffsetMinutes = (arguments["utcOffsetMinutes"] as? NSNumber)?.int16Value,
      validClock(unixTimeSeconds, utcOffsetMinutes)
    else {
      result(FlutterError(
        code: "INVALID_TIME",
        message: text("iPhoneの時刻またはタイムゾーンが不正です。", "The iPhone time or time zone is invalid."),
        details: nil
      ))
      return
    }
    guard session == nil, !sessionRestartPending else {
      result(FlutterError(
        code: "TRANSFER_IN_PROGRESS",
        message: text("別のNFC操作が進行中です。先に中止してください。", "Another NFC operation is active. Cancel it first."),
        details: nil
      ))
      return
    }
    pendingTransfer = PendingTransfer(
      bytes: Data(),
      mode: 0,
      width: 0,
      height: 0,
      crc32: 0,
      transferId: 0,
      unixTimeSeconds: unixTimeSeconds,
      utcOffsetMinutes: utcOffsetMinutes,
      clockOnly: true
    )
    cancelled = false
    communicationRecoveryPending = false
    sessionRestartAttempts = 0
    lastSystemProgressPercent = -1
    terminalAction = nil
    guard beginReaderSession(
      totalBytes: 0,
      message: text("Paper MonoにiPhoneを当ててください。", "Hold your iPhone near Paper Mono.")
    ) else {
      pendingTransfer = nil
      result(FlutterError(
        code: "NFC_SESSION_UNAVAILABLE",
        message: text("NFC読み取りセッションを開始できませんでした。", "The NFC reader session could not be started."),
        details: nil
      ))
      return
    }
    result(nil)
  }

  private func validClock(_ unixTimeSeconds: UInt64, _ utcOffsetMinutes: Int16) -> Bool {
    (UInt64(1_672_531_200)..<UInt64(4_102_444_800)).contains(unixTimeSeconds) &&
      (-840...840).contains(Int(utcOffsetMinutes))
  }

  private func beginReaderSession(totalBytes: Int, message: String) -> Bool {
    guard session == nil,
          let newSession = NFCTagReaderSession(
            pollingOption: .iso14443,
            delegate: self,
            queue: nfcQueue
          )
    else {
      return false
    }
    newSession.alertMessage = lastSystemProgressPercent > 0
      ? text("再接続待ち: \(lastSystemProgressPercent)%まで受信済みです。", "Waiting to reconnect: \(lastSystemProgressPercent)% received.")
      : text("Paper MonoにiPhone上部を当ててください。", "Hold the top of your iPhone near Paper Mono.")
    sessionRestartPending = false
    session = newSession
    emit(phase: "waitingForTag", totalBytes: totalBytes, message: message)
    newSession.begin()
    return true
  }

  func tagReaderSessionDidBecomeActive(_ session: NFCTagReaderSession) {
    guard self.session === session else { return }
    print("[nfc.ios] reader session active")
    session.alertMessage = lastSystemProgressPercent > 0
      ? text("再接続待ち: \(lastSystemProgressPercent)%から再開します。", "Waiting to reconnect: resuming from \(lastSystemProgressPercent)%.")
      : text("Paper Monoを探しています。iPhone上部を当ててください。", "Looking for Paper Mono. Hold the top of your iPhone near it.")
    emit(
      phase: "waitingForTag",
      totalBytes: pendingTransfer?.bytes.count ?? 0,
      message: "PaperMonoを探しています。"
    )
  }

  func tagReaderSession(
    _ session: NFCTagReaderSession,
    didDetect tags: [NFCTag]
  ) {
    guard self.session === session else { return }
    guard let transfer = pendingTransfer else { return }
    sessionRestartAttempts = 0
    communicationRecoveryPending = false
    print("[nfc.ios] detected tags=\(tags.count)")
    guard tags.count == 1, let first = tags.first else {
      session.alertMessage = text("NFCタグを1つだけ近づけてください。", "Hold only one NFC tag nearby.")
      session.restartPolling()
      return
    }
    guard case let .miFare(tag) = first else {
      print("[nfc.ios] rejected non-MIFARE tag=\(first)")
      session.alertMessage = text("Paper MonoのMIFAREタグではありません。", "This is not a Paper Mono MIFARE tag.")
      session.restartPolling()
      return
    }
    // ST25R3916 target emulation presents the Type-A identity and Ultralight
    // command set correctly, but Core NFC's family classification can vary by
    // iOS release while probing an emulated (rather than silicon) tag. Do not
    // reject a usable NFCMiFareTag before sending the protocol HELLO; the
    // magic/version response is the authoritative PaperMono identity check.
    print("[nfc.ios] detected MIFARE family=\(tag.mifareFamily)")

    session.connect(to: first) { [weak self] error in
      guard let self else { return }
      if let error {
        print("[nfc.ios] connect failed error=\(error)")
        self.recoverFromCommunicationError(error.localizedDescription)
        return
      }
      print("[nfc.ios] connected uid=\(tag.identifier.hexString)")
      session.alertMessage = text("Paper Monoに接続しました。送信を開始します。", "Connected to Paper Mono. Starting transfer.")
      self.emit(
        phase: "connected",
        totalBytes: transfer.bytes.count,
        message: "PaperMonoに接続しました。"
      )
      self.activeTag = tag
      self.performTransfer(tag: tag, transfer: transfer)
    }
  }

  func tagReaderSession(
    _ invalidatedSession: NFCTagReaderSession,
    didInvalidateWithError error: Error
  ) {
    print("[nfc.ios] session invalidated error=\(error)")
    // startTransfer can replace an existing reader session. Its asynchronous
    // invalidation callback must not clear the newer session.
    guard session === invalidatedSession else {
      print("[nfc.ios] ignored stale session invalidation")
      return
    }
    self.session = nil
    activeTag = nil
    if let action = terminalAction {
      terminalAction = nil
      completeTerminalAction(action)
      return
    }
    if cancelled {
      pendingTransfer = nil
      sessionRestartPending = false
      completeTerminalAction(.cancelled)
      return
    }
    guard pendingTransfer != nil else { return }

    let errorCode = (error as NSError).code
    let recoverCommunicationSession = communicationRecoveryPending
    communicationRecoveryPending = false
    if (recoverCommunicationSession || errorCode == 202 || errorCode == 203),
       sessionRestartAttempts < Self.automaticRestartLimit
    {
      sessionRestartAttempts += 1
      let attempt = sessionRestartAttempts
      let delay = Self.automaticRestartDelay * Double(attempt)
      sessionRestartPending = true
      print("[nfc.ios] scheduling reader restart attempt=\(attempt) delay=\(delay)s")
      emit(
        phase: "waitingForTag",
        totalBytes: pendingTransfer?.bytes.count ?? 0,
        message: "NFCを再準備しています。ボタンを押さず、そのままお待ちください。"
      )
      nfcQueue.asyncAfter(deadline: .now() + delay) { [weak self] in
        guard let self,
              !self.cancelled,
              self.pendingTransfer != nil,
              self.session == nil,
              self.sessionRestartPending
        else {
          return
        }
        let totalBytes = self.pendingTransfer?.bytes.count ?? 0
        if !self.beginReaderSession(
          totalBytes: totalBytes,
          message: "PaperMonoを探しています。"
        ) {
          self.sessionRestartPending = false
          self.emit(
            phase: "failed",
            totalBytes: totalBytes,
            message: "NFC読み取りセッションを再開できませんでした。",
            errorCode: "NFC_RESTART_FAILED"
          )
        }
      }
      return
    }

    let totalBytes = pendingTransfer?.bytes.count ?? 0
    pendingTransfer = nil
    sessionRestartPending = false
    emit(
      phase: "failed",
      totalBytes: totalBytes,
      message: "NFCセッションを再開できませんでした。もう一度送信ボタンを押してください。",
      errorCode: "SESSION_INVALIDATED"
    )
  }

  private func performTransfer(tag: NFCMiFareTag, transfer: PendingTransfer) {
    guard !cancelled else { return }
    exchange(tag: tag, command: PaperMonoProtocol.hello(), expected: .hello) { [weak self] helloResult in
      guard let self else { return }
      do {
        let hello = try helloResult.get()
        try self.requireStatus(hello, accepted: [.ok])
        let capabilities = try PaperMonoProtocol.parseHello(hello)
        if transfer.clockOnly {
          guard capabilities.supportsTimeSync else {
            throw TransferError(
              code: "TIME_SYNC_UNSUPPORTED",
              message: "Paper Monoが時刻同期に対応していません。"
            )
          }
          self.sendTime(tag: tag, transfer: transfer)
        } else {
          self.sendImage(tag: tag, transfer: transfer, capabilities: capabilities)
        }
      } catch {
        self.failOrRecover(error)
      }
    }
  }

  private func sendTime(
    tag: NFCMiFareTag,
    transfer: PendingTransfer
  ) {
    emit(
      phase: "clockSyncing",
      totalBytes: transfer.bytes.count,
      message: "時刻を同期しています。"
    )
    exchange(
      tag: tag,
      command: PaperMonoProtocol.setTime(
        unixTimeSeconds: transfer.unixTimeSeconds,
        utcOffsetMinutes: transfer.utcOffsetMinutes
      ),
      expected: .setTime
    ) { [weak self] result in
      guard let self else { return }
      do {
        let response = try result.get()
        try self.requireStatus(response, accepted: [.ok])
        self.finishClockSync()
      } catch {
        self.failOrRecover(error)
      }
    }
  }

  private func sendImage(
    tag: NFCMiFareTag,
    transfer: PendingTransfer,
    capabilities: HelloCapabilities
  ) {
    guard transfer.mode != 0x02 || capabilities.supportsFullscreenImage else {
      failOrRecover(TransferError(
        code: "FULLSCREEN_UNSUPPORTED",
        message: "Paper Monoのファームウェアが全画面画像に対応していません。"
      ))
      return
    }
    guard capabilities.maxAcceptedRfFrameBytes >= 63,
          capabilities.maxProtocolCommandBytes >= 61,
          capabilities.maxDataPayloadBytes >= 48,
          capabilities.maxImageBytes >= UInt32(transfer.bytes.count),
          capabilities.supportsBaselineJpeg3Component
    else {
      failOrRecover(TransferError(
        code: "INCOMPATIBLE_LIMITS",
        message: "PaperMonoのDATA上限がv1仕様より小さいです。"
      ))
      return
    }
    let dataPayloadBytes = min(
      Int(capabilities.maxDataPayloadBytes),
      PaperMonoProtocol.maxDataPayloadBytes,
      dataPayloadLimit
    )
    print("[nfc.ios] negotiated DATA payload=\(dataPayloadBytes) bytes")
    sendBegin(
      tag: tag,
      transfer: transfer,
      dataPayloadBytes: dataPayloadBytes
    )
  }

  private func sendBegin(
    tag: NFCMiFareTag,
    transfer: PendingTransfer,
    dataPayloadBytes: Int
  ) {
    exchange(
      tag: tag,
      command: PaperMonoProtocol.begin(transfer),
      expected: .begin
    ) { [weak self] result in
      guard let self else { return }
      do {
        let response = try result.get()
        try self.requireTransferId(response, transfer: transfer)
        try self.requireStatus(
          response,
          accepted: [.ok, .accepted, .receiving, .stored, .displaying, .completed]
        )
        let offset = Int(response.nextExpectedOffset)
        guard offset >= 0, offset <= transfer.bytes.count else {
          throw TransferError(code: "INVALID_OFFSET", message: "PaperMonoが不正な再開位置を返しました。")
        }
        self.updateSystemProgress(bytesSent: offset, totalBytes: transfer.bytes.count)
        self.sendChunk(
          tag: tag,
          transfer: transfer,
          offset: offset,
          stalls: 0,
          dataPayloadBytes: dataPayloadBytes
        )
      } catch {
        self.failOrRecover(error)
      }
    }
  }

  private func sendChunk(
    tag: NFCMiFareTag,
    transfer: PendingTransfer,
    offset: Int,
    stalls: Int,
    dataPayloadBytes: Int
  ) {
    guard !cancelled else { return }
    if offset >= transfer.bytes.count {
      sendCommit(tag: tag, transfer: transfer)
      return
    }
    let end = min(offset + dataPayloadBytes, transfer.bytes.count)
    let payload = transfer.bytes.subdata(in: offset..<end)
    exchange(
      tag: tag,
      command: PaperMonoProtocol.data(
        transferId: transfer.transferId,
        offset: UInt32(offset),
        payload: payload
      ),
      expected: .data
    ) { [weak self] result in
      guard let self else { return }
      do {
        let response = try result.get()
        try self.requireTransferId(response, transfer: transfer)
        try self.requireStatus(response, accepted: [.ok, .receiving, .badOffset])
        let next = Int(response.nextExpectedOffset)
        guard next >= 0, next <= transfer.bytes.count else {
          throw TransferError(code: "INVALID_OFFSET", message: "PaperMonoが不正な受信位置を返しました。")
        }
        let nextStalls = next == offset ? stalls + 1 : 0
        guard nextStalls < 3 else {
          throw TransferError(code: "TRANSFER_STALLED", message: "PaperMonoの受信位置が進みません。")
        }
        self.emit(
          phase: "receiving",
          bytesSent: next,
          totalBytes: transfer.bytes.count,
          nextExpectedOffset: next
        )
        self.updateSystemProgress(bytesSent: next, totalBytes: transfer.bytes.count)
        self.nfcQueue.asyncAfter(deadline: .now() + Self.dataInterCommandDelay) {
          self.sendChunk(
            tag: tag,
            transfer: transfer,
            offset: next,
            stalls: nextStalls,
            dataPayloadBytes: dataPayloadBytes
          )
        }
      } catch {
        if !(error is TransferError) {
          self.reduceDataPayloadAfterTransportFailure()
        }
        self.failOrRecover(error)
      }
    }
  }

  private func sendCommit(tag: NFCMiFareTag, transfer: PendingTransfer) {
    session?.alertMessage = text("送信完了: 100%　受信データを確認しています。", "Transfer complete: 100%. Checking received data.")
    exchange(
      tag: tag,
      command: PaperMonoProtocol.commit(
        transferId: transfer.transferId,
        crc32: transfer.crc32
      ),
      expected: .commit
    ) { [weak self] result in
      guard let self else { return }
      do {
        let response = try result.get()
        try self.requireTransferId(response, transfer: transfer)
        try self.handleCommitStatus(response, transfer: transfer)
        if response.status == .stored ||
           response.status == .displaying ||
           response.status == .completed
        {
          // STORED is the durable protocol success boundary. Production
          // PaperMono deliberately stops servicing RF before its blocking
          // e-paper refresh, so polling for COMPLETED turns a successful
          // transfer into a false tag-loss recovery and contaminates the next
          // reader session.
          self.finish(
            transfer: transfer,
            completed: response.status == .completed
          )
          return
        }
        self.pollCommit(
          tag: tag,
          transfer: transfer,
          deadline: Date().addingTimeInterval(Self.commitTrackSeconds)
        )
      } catch {
        self.failOrRecover(error)
      }
    }
  }

  private func pollCommit(
    tag: NFCMiFareTag,
    transfer: PendingTransfer,
    deadline: Date
  ) {
    guard !cancelled else { return }
    nfcQueue.asyncAfter(deadline: .now() + Self.commitPollDelay) { [weak self] in
      guard let self else { return }
      self.exchange(
        tag: tag,
        command: PaperMonoProtocol.status(transfer.transferId),
        expected: .status
      ) { [weak self] result in
        guard let self else { return }
        do {
          let response = try result.get()
          try self.requireTransferId(response, transfer: transfer)
          try self.handleCommitStatus(response, transfer: transfer)
          let nowStored = response.status == .stored ||
            response.status == .displaying || response.status == .completed
          if response.status == .completed {
            self.finish(transfer: transfer, completed: true)
          } else if nowStored {
            self.finish(transfer: transfer, completed: false)
          } else if Date() >= deadline {
            throw TransferError(code: "COMMIT_TIMEOUT", message: "PaperMonoの保存確認がタイムアウトしました。")
          } else {
            self.pollCommit(
              tag: tag,
              transfer: transfer,
              deadline: deadline
            )
          }
        } catch {
          self.failOrRecover(error)
        }
      }
    }
  }

  private func handleCommitStatus(
    _ response: PaperMonoResponse,
    transfer: PendingTransfer
  ) throws {
    switch response.status {
    case .ok, .accepted, .verifying:
      session?.alertMessage = text("送信完了: 100%　CRCと画像を検証しています。", "Transfer complete: 100%. Verifying CRC and image.")
      emit(
        phase: "verifying",
        bytesSent: transfer.bytes.count,
        totalBytes: transfer.bytes.count,
        nextExpectedOffset: Int(response.nextExpectedOffset)
      )
    case .stored:
      session?.alertMessage = text("画像を保存しました。画面更新を待っています。", "Image stored. Waiting for the display update.")
      emit(
        phase: "stored",
        bytesSent: transfer.bytes.count,
        totalBytes: transfer.bytes.count,
        nextExpectedOffset: Int(response.nextExpectedOffset)
      )
    case .displaying:
      session?.alertMessage = text("Paper Monoの画面を更新しています。", "Updating the Paper Mono display.")
      emit(
        phase: "displaying",
        bytesSent: transfer.bytes.count,
        totalBytes: transfer.bytes.count,
        nextExpectedOffset: Int(response.nextExpectedOffset)
      )
    case .completed:
      break
    case .crcMismatch:
      throw TransferError(code: "CRC_MISMATCH", message: "PaperMonoで画像CRCが一致しませんでした。")
    case .invalidJpeg:
      throw TransferError(code: "INVALID_JPEG", message: "PaperMonoがJPEGを受け付けませんでした。")
    default:
      throw TransferError(
        code: "COMMIT_REJECTED",
        message: "PaperMonoがCOMMITを拒否しました: \(response.status)"
      )
    }
  }

  private func updateSystemProgress(bytesSent: Int, totalBytes: Int) {
    guard totalBytes > 0 else { return }
    let safeBytes = min(max(bytesSent, 0), totalBytes)
    let percent = safeBytes * 100 / totalBytes
    guard lastSystemProgressPercent < 0 ||
          percent >= lastSystemProgressPercent + 5 ||
          percent == 100
    else {
      return
    }
    lastSystemProgressPercent = percent
    session?.alertMessage = String(
      format: text("画像を送信中: %d%%（%.1f / %.1f KB）", "Sending image: %d%% (%.1f / %.1f KB)"),
      percent,
      Double(safeBytes) / 1024.0,
      Double(totalBytes) / 1024.0
    )
  }

  private func exchange(
    tag: NFCMiFareTag,
    command: Data,
    expected: PaperMonoCommand,
    retries: Int = 2,
    completion: @escaping (Result<PaperMonoResponse, Error>) -> Void
  ) {
    guard !cancelled else { return }
    // Core NFC's reader hardware supplies the RF CRC-A for this emulated tag.
    // Passing a software CRC here produced command + CRC-A + RF CRC-A on the
    // PaperMono FIFO and made every protocol request two bytes too long after
    // the ST25R3916 listener removed the RF CRC.
    let packet = command
    if expected != .data {
      print(
        "[nfc.ios] tx command=\(String(format: "%02X", expected.rawValue)) " +
        "bytes=\(packet.count) data=\(packet.hexString)"
      )
    }
    tag.sendMiFareCommand(
      commandPacket: packet
    ) { response, error in
      if let error {
        print("[nfc.ios] tx failed command=\(expected) error=\(error)")
        if retries > 0, tag.isAvailable {
          self.exchange(
            tag: tag,
            command: command,
            expected: expected,
            retries: retries - 1,
            completion: completion
          )
        } else {
          completion(.failure(error))
        }
        return
      }
      if expected != .data {
        print(
          "[nfc.ios] rx command=\(String(format: "%02X", expected.rawValue)) " +
          "bytes=\(response.count) data=\(response.hexString)"
        )
      }
      do {
        completion(.success(try PaperMonoProtocol.parseResponse(response, expected: expected)))
      } catch {
        let parseMessage = (error as? TransferError)?.message ?? error.localizedDescription
        print(
          "[nfc.ios] invalid response command=\(expected) bytes=\(response.count) " +
          "data=\(response.hexString) retries=\(retries) error=\(parseMessage)"
        )
        // A truncated response is a transport failure, not a fatal protocol
        // rejection. Every v1 command is idempotent, so retry the exact same
        // command; if the tag has already dropped, return a non-TransferError
        // so failOrRecover() restarts polling and BEGIN resumes at the
        // PaperMono-provided nextExpectedOffset.
        if retries > 0, tag.isAvailable {
          self.exchange(
            tag: tag,
            command: command,
            expected: expected,
            retries: retries - 1,
            completion: completion
          )
        } else {
          completion(.failure(NSError(
            domain: "PaperMonoNfcTransport",
            code: 1,
            userInfo: [NSLocalizedDescriptionKey: parseMessage]
          )))
        }
      }
    }
  }

  private func requireTransferId(
    _ response: PaperMonoResponse,
    transfer: PendingTransfer
  ) throws {
    guard response.transferId == transfer.transferId else {
      print(
        "[nfc.ios] transfer id mismatch expected=\(transfer.transferId) " +
        "actual=\(response.transferId)"
      )
      throw TransferError(code: "TRANSFER_ID_MISMATCH", message: "PaperMonoの転送IDが一致しません。")
    }
  }

  private func reduceDataPayloadAfterTransportFailure() {
    guard dataPayloadLimit > Self.fallbackDataPayloadBytes else { return }
    let previous = dataPayloadLimit
    dataPayloadLimit = Self.fallbackDataPayloadBytes
    print("[nfc.ios] DATA transport fallback payload=\(previous)->\(dataPayloadLimit)")
  }

  private func requireStatus(
    _ response: PaperMonoResponse,
    accepted: Set<PaperMonoStatus>
  ) throws {
    guard accepted.contains(response.status) else {
      throw TransferError(
        code: String(describing: response.status),
        message: "PaperMonoがコマンドを拒否しました: \(response.status)"
      )
    }
  }

  private func finish(transfer: PendingTransfer, completed: Bool) {
    session?.alertMessage = completed
      ? text("Paper Monoへの画像送信が完了しました。", "Image transfer to Paper Mono completed.")
      : text("画像をPaper Monoへ保存しました。", "Image stored on Paper Mono.")
    pendingTransfer = nil
    activeTag = nil
    endSession(.finished(transfer: transfer, displayConfirmed: completed))
  }

  private func finishClockSync() {
    session?.alertMessage = text("Paper Monoの時刻を同期しました。", "Paper Mono clock synchronized.")
    pendingTransfer = nil
    activeTag = nil
    endSession(.clockSynced)
  }

  private func finishCancellation() {
    pendingTransfer = nil
    activeTag = nil
    endSession(.cancelled)
  }

  private func endSession(
    _ action: SessionTerminalAction,
    errorMessage: String? = nil
  ) {
    sessionRestartPending = false
    communicationRecoveryPending = false
    terminalAction = action
    guard let activeSession = session else {
      terminalAction = nil
      completeTerminalAction(action)
      return
    }
    if let errorMessage {
      activeSession.invalidate(errorMessage: errorMessage)
    } else {
      activeSession.invalidate()
    }
  }

  private func completeTerminalAction(_ action: SessionTerminalAction) {
    switch action {
    case let .finished(transfer, displayConfirmed):
      emit(
        phase: "completed",
        bytesSent: transfer.bytes.count,
        totalBytes: transfer.bytes.count,
        nextExpectedOffset: transfer.bytes.count,
        message: displayConfirmed
          ? "PaperMonoへの画像送信が完了しました。"
          : "画像は保存済みです。画面更新の完了確認を終了しました。"
      )
    case let .failed(totalBytes, message, errorCode):
      emit(
        phase: "failed",
        totalBytes: totalBytes,
        message: message,
        errorCode: errorCode
      )
    case .cancelled:
      cancelled = false
      emit(phase: "idle", message: "送信を中止しました。")
    case .clockSynced:
      emit(phase: "clockSynced", message: "Paper Monoの時刻を同期しました。")
    }
  }

  private func failOrRecover(_ error: Error) {
    guard !cancelled else { return }
    if let transferError = error as? TransferError {
      if transferError.code == "TRANSFER_ID_MISMATCH" {
        recoverFromCommunicationError(transferError.message)
        return
      }
      let totalBytes = pendingTransfer?.bytes.count ?? 0
      let message = localizedError(code: transferError.code, fallback: transferError.message)
      pendingTransfer = nil
      activeTag = nil
      endSession(
        .failed(
          totalBytes: totalBytes,
          message: message,
          errorCode: transferError.code
        ),
        errorMessage: message
      )
      return
    }
    recoverFromCommunicationError(error.localizedDescription)
  }

  private func recoverFromCommunicationError(_ message: String) {
    activeTag = nil
    communicationRecoveryPending = true
    emit(
      phase: "recoverableError",
      totalBytes: pendingTransfer?.bytes.count ?? 0,
      message: "\(message) もう一度PaperMonoへ当てると途中から再開します。",
      errorCode: "TAG_LOST"
    )
    session?.alertMessage = text("接続が切れました。もう一度Paper Monoへ当ててください。", "Connection lost. Hold your iPhone near Paper Mono again.")
    emit(
      phase: "waitingForTag",
      totalBytes: pendingTransfer?.bytes.count ?? 0,
      message: "NFCを再準備しています。そのままPaperMonoへ当ててください。"
    )
    // After NFCError 100, restartPolling() on the connected reader session can
    // remain silent indefinitely: the RF field is already down and Core NFC
    // never emits another didDetect callback. Invalidate that broken session;
    // didInvalidateWithError observes communicationRecoveryPending and starts
    // a fresh reader session, whose BEGIN resumes from nextExpectedOffset.
    if let activeSession = session {
      print("[nfc.ios] invalidating lost-tag session for automatic restart")
      activeSession.invalidate()
    } else {
      print("[nfc.ios] lost-tag recovery has no active session")
    }
  }

  private func emit(
    phase: String,
    bytesSent: Int = 0,
    totalBytes: Int = 0,
    nextExpectedOffset: Int = 0,
    message: String? = nil,
    errorCode: String? = nil
  ) {
    var event: [String: Any] = [
      "phase": phase,
      "bytesSent": bytesSent,
      "totalBytes": totalBytes,
      "nextExpectedOffset": nextExpectedOffset,
    ]
    if let message { event["message"] = message }
    if let errorCode { event["errorCode"] = errorCode }
    DispatchQueue.main.async { [weak self] in self?.eventSink?(event) }
  }
}

@available(iOS 13.0, *)
private struct PendingTransfer {
  let bytes: Data
  let mode: UInt8
  let width: UInt16
  let height: UInt16
  let crc32: UInt32
  let transferId: UInt32
  let unixTimeSeconds: UInt64
  let utcOffsetMinutes: Int16
  let clockOnly: Bool
}

@available(iOS 13.0, *)
private enum SessionTerminalAction {
  case finished(transfer: PendingTransfer, displayConfirmed: Bool)
  case failed(totalBytes: Int, message: String, errorCode: String)
  case cancelled
  case clockSynced
}

@available(iOS 13.0, *)
private enum PaperMonoCommand: UInt8 {
  case hello = 0x01
  case begin = 0x02
  case data = 0x03
  case status = 0x04
  case commit = 0x05
  case abort = 0x06
  case setTime = 0x07
}

@available(iOS 13.0, *)
private enum PaperMonoStatus: UInt8, Hashable {
  case ok = 0x00
  case accepted = 0x01
  case busy = 0x02
  case conflict = 0x03
  case badMagic = 0x04
  case unsupportedVersion = 0x05
  case unknownCommand = 0x06
  case invalidLength = 0x07
  case payloadTooLarge = 0x08
  case imageTooLarge = 0x09
  case badOffset = 0x0a
  case dataMismatch = 0x0b
  case crcMismatch = 0x0c
  case invalidJpeg = 0x0d
  case unsupportedFormat = 0x0e
  case notFound = 0x0f
  case internalError = 0x10
  case receiving = 0x11
  case verifying = 0x12
  case stored = 0x13
  case displaying = 0x14
  case completed = 0x15
}

@available(iOS 13.0, *)
private struct PaperMonoResponse {
  let status: PaperMonoStatus
  let transferId: UInt32
  let nextExpectedOffset: UInt32
  let extra: Data
}

@available(iOS 13.0, *)
private struct HelloCapabilities {
  let maxAcceptedRfFrameBytes: UInt16
  let maxProtocolCommandBytes: UInt16
  let maxDataPayloadBytes: UInt16
  let maxImageBytes: UInt32
  let supportsBaselineJpeg3Component: Bool
  let supportsTimeSync: Bool
  let supportsFullscreenImage: Bool
}

private struct TransferError: Error {
  let code: String
  let message: String
}

@available(iOS 13.0, *)
private enum PaperMonoProtocol {
  static let magic0: UInt8 = 0x50
  static let magic1: UInt8 = 0x4d
  static let version: UInt8 = 0x01
  static let maxDataPayloadBytes = 240
  static let maxImageBytes = 262_144
  static let responseEnvelopeBytes = 13

  static func hello() -> Data {
    prefix(.hello)
  }

  static func begin(_ transfer: PendingTransfer) -> Data {
    var data = prefix(.begin)
    data.appendLittleEndian(transfer.transferId)
    data.append(1) // BEGIN(REPLACE); a matching transfer still resumes
    data.append(transfer.mode)
    data.append(1) // Baseline JPEG, 3 components
    data.appendLittleEndian(transfer.width)
    data.appendLittleEndian(transfer.height)
    data.appendLittleEndian(UInt32(transfer.bytes.count))
    data.appendLittleEndian(transfer.crc32)
    return data
  }

  static func data(transferId: UInt32, offset: UInt32, payload: Data) -> Data {
    precondition(payload.count <= maxDataPayloadBytes)
    var data = prefix(.data)
    data.appendLittleEndian(transferId)
    data.appendLittleEndian(offset)
    data.append(UInt8(payload.count))
    data.append(payload)
    return data
  }

  static func status(_ transferId: UInt32) -> Data {
    var data = prefix(.status)
    data.appendLittleEndian(transferId)
    return data
  }

  static func commit(transferId: UInt32, crc32: UInt32) -> Data {
    var data = prefix(.commit)
    data.appendLittleEndian(transferId)
    data.appendLittleEndian(crc32)
    return data
  }

  static func abort(_ transferId: UInt32) -> Data {
    var data = prefix(.abort)
    data.appendLittleEndian(transferId)
    return data
  }

  static func setTime(unixTimeSeconds: UInt64, utcOffsetMinutes: Int16) -> Data {
    var data = prefix(.setTime)
    data.appendLittleEndian(unixTimeSeconds)
    data.appendLittleEndian(utcOffsetMinutes)
    data.append(0) // Reserved flags.
    return data
  }

  static func parseResponse(
    _ bytes: Data,
    expected: PaperMonoCommand
  ) throws -> PaperMonoResponse {
    guard bytes.count >= responseEnvelopeBytes else {
      throw TransferError(code: "SHORT_RESPONSE", message: "PaperMonoの応答が13バイト未満です。")
    }
    guard bytes[0] == magic0, bytes[1] == magic1 else {
      throw TransferError(code: "BAD_MAGIC", message: "PaperMono応答のMagicが一致しません。")
    }
    guard bytes[2] == version else {
      throw TransferError(code: "BAD_VERSION", message: "PaperMonoのプロトコルバージョンが一致しません。")
    }
    guard bytes[3] == (expected.rawValue | 0x80) else {
      throw TransferError(code: "BAD_COMMAND", message: "PaperMonoの応答コマンドが一致しません。")
    }
    guard let status = PaperMonoStatus(rawValue: bytes[4]) else {
      throw TransferError(code: "UNKNOWN_STATUS", message: "PaperMonoが未知のステータスを返しました。")
    }
    return PaperMonoResponse(
      status: status,
      transferId: bytes.readUInt32LittleEndian(at: 5),
      nextExpectedOffset: bytes.readUInt32LittleEndian(at: 9),
      extra: bytes.subdata(in: responseEnvelopeBytes..<bytes.count)
    )
  }

  static func parseHello(_ response: PaperMonoResponse) throws -> HelloCapabilities {
    guard response.extra.count >= 11 else {
      throw TransferError(code: "SHORT_HELLO", message: "PaperMonoのHELLO応答が短すぎます。")
    }
    let capabilityBits = response.extra[10]
    let capabilities = HelloCapabilities(
      maxAcceptedRfFrameBytes: response.extra.readUInt16LittleEndian(at: 0),
      maxProtocolCommandBytes: response.extra.readUInt16LittleEndian(at: 2),
      maxDataPayloadBytes: response.extra.readUInt16LittleEndian(at: 4),
      maxImageBytes: response.extra.readUInt32LittleEndian(at: 6),
      supportsBaselineJpeg3Component: capabilityBits & 0x01 != 0,
      supportsTimeSync: capabilityBits & 0x02 != 0,
      supportsFullscreenImage: capabilityBits & 0x04 != 0
    )
    guard capabilities.maxAcceptedRfFrameBytes <= 255,
          capabilities.maxProtocolCommandBytes <= 253,
          capabilities.maxDataPayloadBytes <= UInt16(maxDataPayloadBytes),
          capabilities.maxImageBytes <= UInt32(maxImageBytes)
    else {
      throw TransferError(code: "UNSAFE_HELLO", message: "PaperMonoがv1の安全上限を超える能力値を返しました。")
    }
    return capabilities
  }

  private static func prefix(_ command: PaperMonoCommand) -> Data {
    Data([magic0, magic1, version, command.rawValue])
  }
}

private extension Data {
  var hexString: String {
    map { String(format: "%02X", $0) }.joined()
  }

  mutating func appendLittleEndian(_ value: UInt16) {
    append(UInt8(value & 0xff))
    append(UInt8((value >> 8) & 0xff))
  }

  mutating func appendLittleEndian(_ value: UInt32) {
    append(UInt8(value & 0xff))
    append(UInt8((value >> 8) & 0xff))
    append(UInt8((value >> 16) & 0xff))
    append(UInt8((value >> 24) & 0xff))
  }

  mutating func appendLittleEndian(_ value: UInt64) {
    for shift in stride(from: 0, through: 56, by: 8) {
      append(UInt8((value >> UInt64(shift)) & 0xff))
    }
  }

  mutating func appendLittleEndian(_ value: Int16) {
    appendLittleEndian(UInt16(bitPattern: value))
  }

  func readUInt16LittleEndian(at offset: Int) -> UInt16 {
    UInt16(self[offset]) | UInt16(self[offset + 1]) << 8
  }

  func readUInt32LittleEndian(at offset: Int) -> UInt32 {
    UInt32(self[offset]) |
      UInt32(self[offset + 1]) << 8 |
      UInt32(self[offset + 2]) << 16 |
      UInt32(self[offset + 3]) << 24
  }
}
