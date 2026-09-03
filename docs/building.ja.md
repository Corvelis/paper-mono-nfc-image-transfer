# ビルドと書き込み

この文書はソースから開発ビルドする手順です。GitHub Releaseの単一BINや署名済み
APKを使う場合は[配布バイナリのインストール](install_binary.ja.md)を参照してください。

## 対象ハードウェア

本ファームウェアはNFC内蔵のM5Stack Paper Mono C153専用です。Paper Mono
LiteにはNFCハードウェアがないため対応しません。

## ファームウェア

PlatformIO Coreを用意し、Paper MonoをUSB接続して次を実行します。

```sh
cd firmware
pio run -e paper-mono
pio run -e paper-mono -t upload
```

`firmware/assets/default.jpg`はビルド時に検証してバイナリへ埋め込まれます。
LittleFSの別書き込みは不要です。画像を差し替える場合は386 x 386、8-bit、
3コンポーネントのBaseline JPEG、256 KiB以下にしてください。不適合なら
ビルドを停止します。

シリアルログは115200 bpsです。

```sh
pio device monitor -b 115200
```

## Android

```sh
cd mobile
flutter pub get
flutter analyze
flutter test
flutter build apk --debug
```

Android 7.0（API 24）以上とNFC-A対応端末が必要です。アプリはNFC権限だけを
宣言し、インターネット権限は宣言しません。

## iPhone

iOS 13以上のNFC対応iPhoneとmacOS/Xcode/CocoaPodsが必要です。

```sh
cd mobile
flutter pub get
flutter build ios --no-codesign
open ios/Runner.xcworkspace
```

Xcodeで各開発者のTeamを選択し、NFC Tag Reading capabilityを確認してから
実機へビルドします。Team IDやProvisioning Profileはリポジトリへ追加しません。

## 最初の動作確認

1. ファームウェアだけを書き込み、埋め込み画像とダッシュボードが出ることを確認する。
2. BtnBでフロントライトが消え、再度BtnBで復帰することを確認する。
3. BtnAを約700 ms長押しし、`RECEIVE IMAGE`へ入る。
4. アプリから386 x 386のDASH画像または480 x 800のFULL画像を送り、時刻同期なしでも成功することを確認する。
5. 送信中にスマホを離して再接続し、転送が再開することを確認する。
6. `SYNC CLOCK`で時刻だけを送り、時計とローカル日付が更新されることを確認する。
7. `IMAGE LIBRARY`でDASH/FULL画像を切り替え、複数選択削除を確認する。
8. `RESET IMAGE`で埋め込み画像へ戻り、歩数・履歴・目標・時刻が残ることを確認する。
