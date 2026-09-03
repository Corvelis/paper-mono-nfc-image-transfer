enum PaperMonoImageMode {
  dateTime(code: 0x01, width: 386, height: 386),
  fullScreen(code: 0x02, width: 480, height: 800);

  const PaperMonoImageMode({
    required this.code,
    required this.width,
    required this.height,
  });

  final int code;
  final int width;
  final int height;
  double get aspectRatio => width / height;

  static PaperMonoImageMode fromName(String name) {
    return values.firstWhere(
      (mode) => mode.name == name,
      orElse: () => dateTime,
    );
  }
}
