enum PaperMonoImageMode {
  dateTime(
    code: 0x01,
    width: 386,
    height: 386,
    label: 'ダッシュボード',
    description: '画像、時計、カレンダー、歩数と一緒に表示',
  );

  const PaperMonoImageMode({
    required this.code,
    required this.width,
    required this.height,
    required this.label,
    required this.description,
  });

  final int code;
  final int width;
  final int height;
  final String label;
  final String description;

  double get aspectRatio => width / height;

  static PaperMonoImageMode fromName(String name) {
    return values.firstWhere(
      (mode) => mode.name == name,
      orElse: () => dateTime,
    );
  }
}
