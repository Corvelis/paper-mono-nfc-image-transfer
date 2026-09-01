import 'dart:async';
import 'dart:typed_data';

import 'package:crop_your_image/crop_your_image.dart';
import 'package:flutter/material.dart';
import 'package:image_picker/image_picker.dart';

import 'features/home/image_workflow_controller.dart';
import 'features/transfer/nfc_transfer_bridge.dart';

class PaperMonoSenderApp extends StatelessWidget {
  const PaperMonoSenderApp({super.key});

  @override
  Widget build(BuildContext context) {
    final colorScheme = ColorScheme.fromSeed(
      seedColor: const Color(0xff202124),
      brightness: Brightness.light,
      surface: const Color(0xfff7f5ef),
    );
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      title: 'Paper Mono Image Sender',
      theme: ThemeData(
        colorScheme: colorScheme,
        scaffoldBackgroundColor: const Color(0xffefede6),
        useMaterial3: true,
        cardTheme: const CardThemeData(
          elevation: 0,
          color: Color(0xfffaf9f5),
          margin: EdgeInsets.zero,
        ),
        filledButtonTheme: FilledButtonThemeData(
          style: FilledButton.styleFrom(
            minimumSize: const Size.fromHeight(52),
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(14),
            ),
          ),
        ),
      ),
      home: const HomeScreen(),
    );
  }
}

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  late final ImageWorkflowController _workflow;
  CropController _cropController = CropController();
  CropStatus _cropStatus = CropStatus.nothing;
  Uint8List? _lastCropSource;
  final Set<int> _cropPointers = <int>{};

  bool get _isCropGestureActive => _cropPointers.isNotEmpty;

  @override
  void initState() {
    super.initState();
    _workflow = ImageWorkflowController()..addListener(_refresh);
    unawaited(_workflow.initialize());
  }

  void _refresh() {
    if (mounted) {
      final source = _workflow.sourceBytes;
      setState(() {
        if (!identical(source, _lastCropSource)) {
          _lastCropSource = source;
          _cropStatus = source == null
              ? CropStatus.nothing
              : CropStatus.loading;
          _cropController = CropController();
        }
      });
    }
  }

  void _onCropStatusChanged(CropStatus status) {
    if (mounted && _cropStatus != status) {
      setState(() {
        _cropStatus = status;
      });
    }
  }

  void _onCropPointerDown(PointerDownEvent event) {
    final wasActive = _isCropGestureActive;
    _cropPointers.add(event.pointer);
    if (!wasActive && mounted) {
      setState(() {});
    }
  }

  void _onCropPointerEnd(PointerEvent event) {
    _cropPointers.remove(event.pointer);
    if (!_isCropGestureActive && mounted) {
      setState(() {});
    }
  }

  void _onCropped(CropResult result) {
    switch (result) {
      case CropSuccess(:final croppedImage):
        unawaited(_workflow.prepare(croppedImage));
      case CropFailure(:final cause):
        ScaffoldMessenger.of(
          context,
        ).showSnackBar(SnackBar(content: Text('クロップに失敗しました: $cause')));
    }
  }

  @override
  void dispose() {
    _workflow
      ..removeListener(_refresh)
      ..dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final source = _workflow.sourceBytes;
    final prepared = _workflow.preparedImage;
    final transfer = _workflow.transferEvent;

    return Scaffold(
      appBar: AppBar(
        title: const Text('Paper Mono Image Sender'),
        centerTitle: false,
        backgroundColor: Colors.transparent,
      ),
      bottomNavigationBar: transfer == null
          ? null
          : _TransferStatusPanel(
              event: transfer,
              onCancel: _workflow.cancelTransfer,
              onDismiss: _workflow.clearTransferStatus,
            ),
      body: SafeArea(
        child: ListView(
          physics: _isCropGestureActive
              ? const NeverScrollableScrollPhysics()
              : const ClampingScrollPhysics(),
          padding: const EdgeInsets.fromLTRB(16, 4, 16, 32),
          children: <Widget>[
            _IntroCard(nfcAvailable: _workflow.nfcAvailable),
            const SizedBox(height: 16),
            _SectionCard(
              title: '時計を合わせる',
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: <Widget>[
                  const Text('スマートフォンの現在時刻とタイムゾーンをNFCで送ります。'),
                  const SizedBox(height: 12),
                  OutlinedButton.icon(
                    onPressed: _workflow.isTransferSessionActive
                        ? null
                        : _workflow.syncClock,
                    icon: const Icon(Icons.schedule),
                    label: const Text('NFCで時刻を同期'),
                  ),
                ],
              ),
            ),
            const SizedBox(height: 16),
            _SectionCard(
              title: '1. 画像を選ぶ',
              child: Row(
                children: <Widget>[
                  Expanded(
                    child: OutlinedButton.icon(
                      onPressed: _workflow.isBusy
                          ? null
                          : () => _workflow.pick(ImageSource.gallery),
                      icon: const Icon(Icons.photo_library_outlined),
                      label: const Text('ギャラリー'),
                    ),
                  ),
                  const SizedBox(width: 12),
                  Expanded(
                    child: OutlinedButton.icon(
                      onPressed: _workflow.isBusy
                          ? null
                          : () => _workflow.pick(ImageSource.camera),
                      icon: const Icon(Icons.photo_camera_outlined),
                      label: const Text('カメラ'),
                    ),
                  ),
                ],
              ),
            ),
            if (source != null) ...<Widget>[
              const SizedBox(height: 16),
              _SectionCard(
                title: '2. 切り抜きを調整',
                child: Column(
                  children: <Widget>[
                    Listener(
                      behavior: HitTestBehavior.opaque,
                      onPointerDown: _onCropPointerDown,
                      onPointerUp: _onCropPointerEnd,
                      onPointerCancel: _onCropPointerEnd,
                      child: Container(
                        height: 390,
                        clipBehavior: Clip.antiAlias,
                        decoration: BoxDecoration(
                          color: Colors.black,
                          borderRadius: BorderRadius.circular(12),
                        ),
                        child: Crop(
                          key: ValueKey(
                            '${identityHashCode(source)}-${_workflow.mode.name}',
                          ),
                          image: source,
                          controller: _cropController,
                          onCropped: _onCropped,
                          onStatusChanged: _onCropStatusChanged,
                          aspectRatio: _workflow.mode.aspectRatio,
                          interactive: true,
                          fixCropRect: true,
                          baseColor: Colors.black,
                          maskColor: Colors.black.withValues(alpha: 0.62),
                          radius: 4,
                          progressIndicator: const Center(
                            child: CircularProgressIndicator(),
                          ),
                        ),
                      ),
                    ),
                    const SizedBox(height: 12),
                    FilledButton.icon(
                      onPressed:
                          _workflow.isBusy || _cropStatus != CropStatus.ready
                          ? null
                          : () => _cropController.crop(),
                      icon: const Icon(Icons.tonality_outlined),
                      label: const Text('モノクロプレビューを生成'),
                    ),
                  ],
                ),
              ),
            ],
            if (prepared != null) ...<Widget>[
              const SizedBox(height: 16),
              _SectionCard(
                title: '3. 送信プレビュー',
                child: Column(
                  children: <Widget>[
                    Center(
                      child: Container(
                        constraints: const BoxConstraints(maxHeight: 430),
                        decoration: BoxDecoration(
                          border: Border.all(color: Colors.black26),
                          color: Colors.white,
                        ),
                        child: AspectRatio(
                          aspectRatio: prepared.mode.aspectRatio,
                          child: Image.memory(
                            prepared.bytes,
                            fit: BoxFit.contain,
                            gaplessPlayback: true,
                          ),
                        ),
                      ),
                    ),
                    const SizedBox(height: 12),
                    Row(
                      mainAxisAlignment: MainAxisAlignment.spaceBetween,
                      children: <Widget>[
                        Text(
                          '${(prepared.bytes.length / 1024).toStringAsFixed(1)} KB',
                        ),
                        Text(
                          'CRC32 ${prepared.crc32.toRadixString(16).padLeft(8, '0').toUpperCase()}',
                          style: const TextStyle(
                            fontFeatures: <FontFeature>[
                              FontFeature.tabularFigures(),
                            ],
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 12),
                    FilledButton.icon(
                      // Keep this action tappable even if the startup NFC
                      // availability probe was transiently false. The native
                      // start call returns a visible, specific error and also
                      // gives us an unambiguous tap trace.
                      onPressed: _workflow.isTransferSessionActive
                          ? null
                          : _workflow.send,
                      icon: const Icon(Icons.nfc),
                      label: const Text('NFCで送信'),
                    ),
                  ],
                ),
              ),
            ],
            if (_workflow.isBusy) ...<Widget>[
              const SizedBox(height: 16),
              const LinearProgressIndicator(),
            ],
            if (_workflow.errorMessage != null) ...<Widget>[
              const SizedBox(height: 16),
              MaterialBanner(
                content: Text(_workflow.errorMessage!),
                leading: const Icon(Icons.error_outline),
                actions: <Widget>[
                  TextButton(
                    onPressed: _workflow.clearError,
                    child: const Text('閉じる'),
                  ),
                ],
              ),
            ],
          ],
        ),
      ),
    );
  }
}

class _IntroCard extends StatelessWidget {
  const _IntroCard({required this.nfcAvailable});

  final bool nfcAvailable;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(18),
        child: Row(
          children: <Widget>[
            CircleAvatar(
              backgroundColor: nfcAvailable
                  ? Theme.of(context).colorScheme.primary
                  : Theme.of(context).colorScheme.error,
              foregroundColor: Colors.white,
              child: const Icon(Icons.nfc),
            ),
            const SizedBox(width: 14),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: <Widget>[
                  Text(
                    nfcAvailable ? 'この端末ではNFCを利用できます' : 'この端末ではNFCを利用できません',
                    style: Theme.of(context).textTheme.titleMedium,
                  ),
                  const SizedBox(height: 4),
                  const Text('画像を選び、表示範囲を調整してPaper Monoへ送ります。'),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _SectionCard extends StatelessWidget {
  const _SectionCard({required this.title, required this.child});

  final String title;
  final Widget child;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: <Widget>[
            Text(title, style: Theme.of(context).textTheme.titleMedium),
            const SizedBox(height: 12),
            child,
          ],
        ),
      ),
    );
  }
}

class _TransferStatusPanel extends StatelessWidget {
  const _TransferStatusPanel({
    required this.event,
    required this.onCancel,
    required this.onDismiss,
  });

  final NfcTransferEvent event;
  final VoidCallback onCancel;
  final VoidCallback onDismiss;

  @override
  Widget build(BuildContext context) {
    final terminal =
        event.phase == NfcTransferPhase.completed ||
        event.phase == NfcTransferPhase.failed ||
        event.phase == NfcTransferPhase.clockSynced;
    final progress = event.progress.clamp(0.0, 1.0);
    final percent = (progress * 100).toStringAsFixed(0);
    final hasProgress = event.totalBytes > 0;
    return Material(
      elevation: 12,
      color: Theme.of(context).colorScheme.surface,
      child: SafeArea(
        top: false,
        child: Padding(
          padding: const EdgeInsets.fromLTRB(16, 12, 16, 10),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: <Widget>[
              Row(
                children: <Widget>[
                  Icon(_phaseIcon(event.phase), size: 22),
                  const SizedBox(width: 10),
                  Expanded(
                    child: Text(
                      _phaseLabel(event.phase),
                      style: Theme.of(context).textTheme.titleMedium,
                    ),
                  ),
                  if (hasProgress)
                    Text(
                      '$percent%',
                      style: Theme.of(context).textTheme.titleLarge?.copyWith(
                        fontFeatures: const <FontFeature>[
                          FontFeature.tabularFigures(),
                        ],
                        fontWeight: FontWeight.w700,
                      ),
                    ),
                ],
              ),
              const SizedBox(height: 10),
              LinearProgressIndicator(
                minHeight: 8,
                borderRadius: BorderRadius.circular(4),
                value: hasProgress ? progress : null,
              ),
              const SizedBox(height: 8),
              Row(
                children: <Widget>[
                  Expanded(
                    child: Text(
                      hasProgress
                          ? '${_formatBytes(event.bytesSent)} / '
                                '${_formatBytes(event.totalBytes)}'
                          : event.message ?? '',
                      style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                        fontFeatures: const <FontFeature>[
                          FontFeature.tabularFigures(),
                        ],
                      ),
                    ),
                  ),
                  TextButton(
                    onPressed: terminal ? onDismiss : onCancel,
                    child: Text(terminal ? '閉じる' : '送信を中止'),
                  ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }

  String _formatBytes(int bytes) => '${(bytes / 1024).toStringAsFixed(1)} KB';

  IconData _phaseIcon(NfcTransferPhase phase) => switch (phase) {
    NfcTransferPhase.completed => Icons.check_circle,
    NfcTransferPhase.clockSynced => Icons.check_circle,
    NfcTransferPhase.clockSyncing => Icons.schedule,
    NfcTransferPhase.failed => Icons.error,
    NfcTransferPhase.recoverableError => Icons.sync_problem,
    NfcTransferPhase.verifying => Icons.fact_check_outlined,
    NfcTransferPhase.stored => Icons.save_outlined,
    NfcTransferPhase.displaying => Icons.monitor_outlined,
    NfcTransferPhase.waitingForTag => Icons.nfc,
    NfcTransferPhase.connected || NfcTransferPhase.receiving => Icons.sync,
    NfcTransferPhase.idle => Icons.hourglass_empty,
  };

  String _phaseLabel(NfcTransferPhase phase) => switch (phase) {
    NfcTransferPhase.idle => '待機中',
    NfcTransferPhase.waitingForTag => 'PaperMonoにスマートフォンを当ててください',
    NfcTransferPhase.connected => 'PaperMonoに接続しました',
    NfcTransferPhase.clockSyncing => '時刻を同期しています',
    NfcTransferPhase.clockSynced => '時刻を同期しました',
    NfcTransferPhase.receiving => '画像を送信しています',
    NfcTransferPhase.verifying => 'CRCとJPEGを検証しています',
    NfcTransferPhase.stored => '画像の保存が完了しました',
    NfcTransferPhase.displaying => 'PaperMonoの画面を更新しています',
    NfcTransferPhase.completed => '送信と画面更新が完了しました',
    NfcTransferPhase.recoverableError => '接続が切れました。もう一度当ててください',
    NfcTransferPhase.failed => '送信に失敗しました',
  };
}
