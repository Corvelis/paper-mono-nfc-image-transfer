# Paper Mono NFC Image Transfer

日本語 | [English](README.en.md)

AndroidまたはiPhoneからM5Stack Paper MonoへNFCで画像を送り、時計、
カレンダー、歩数、変更可能な歩数目標、30日履歴と一緒に電子ペーパーへ
表示するオフライン対応プロジェクトです。

対象はNFCを搭載する **M5Stack Paper Mono C153** です。NFCを搭載しない
Paper Mono Liteには対応しません。本プロジェクトは非公式のコミュニティ実装です。

Paper Mono固有部分を
[`Corvelis/stackchan-pet-fw`](https://github.com/Corvelis/stackchan-pet-fw)
から独立させ、共有のマルチデバイスファームウェアへ保守負担を持ち込まずに
開発できる構成にしています。

## リポジトリ構成

- `firmware/`: Paper Mono C153専用のPlatformIOファームウェア
- `mobile/`: Android `NfcA`とiPhone Core NFCに対応する共通Flutterアプリ
- `protocol/`: バージョン管理されたNFC通信仕様と共通テストベクター
- `docs/`: 製品仕様、設計、書き込み、配布、試験資料

Wi-Fi、Bluetooth、クラウドアカウント、解析、OTA更新は使用しません。

## デバイスでの操作

既存のPaper Monoのデザインを踏襲したダッシュボードへ、次を表示します。

- 埋め込みデフォルト画像または最後に確定したNFC受信画像
- 大きなローカル時計と日付
- 7日間ストリップと月間カレンダー
- 今日の歩数、変更可能な目標、セグメント式の達成カウンター
- デバイス内に保存する30日分の歩数履歴
- RTCと歩数計測を継続する、フロントライト消灯の省電力ロック

通常画面で`BtnA`を約0.7秒長押しすると、6項目のメニューを開きます。

```text
RECEIVE IMAGE    SYNC CLOCK
STEP GOAL        STEP HISTORY
RESET IMAGE      BACK
```

通常画面で`BtnB`を押すと省電力ロックへ入り、もう一度押すと復帰します。
電源キーはデバイスの電源制御用として予約します。

## 画像転送と時刻同期

Protocol v1ではスマートフォンからPaper Monoへ画像を送ります。Paper Monoから
スマートフォンへ返すのはACK、進捗、エラーであり、画像のダウンロードは行いません。

- ダッシュボード画像: 386 x 386ピクセル
- JPEG: Baseline、3コンポーネント、メタデータなし
- エンコード後の上限: 256 KiB
- 更新方法: LittleFSのA/Bスロットへ保存し、完了後にアトミックに切り替え
- 時刻: スマートフォンのUTC時刻とUTCオフセットをRX8130CE RTCへ設定

転送が中断した場合や画像が不正だった場合は、それまでの正常画像を維持します。
`RESET IMAGE`はNFC受信画像の両スロットを削除し、ファームウェアへ埋め込んだ
デフォルト画像へ戻します。

## インストール

[GitHub Releases](https://github.com/Corvelis/paper-mono-nfc-image-transfer/releases/latest)
では、初期画像込みの初回用`full.bin`、保存データを維持する更新用`app.bin`、
署名済みAndroid APK、Google Play用AAB、SHA-256一覧をバージョン単位で配布します。

- Paper MonoとAndroid: [配布バイナリのインストール手順](docs/install_binary.ja.md)
- iPhone: TestFlight/App Store公開後は公式リンクを掲載。それまではXcodeで実機ビルド
- メンテナー: [署名設定とGitHub Release作成](docs/releasing.ja.md)

GitHub Actionsの通常CIにあるAndroid APKはデバッグ用です。一般利用・再配布には
GitHub Releaseの署名済み`android.apk`を使用してください。

## ビルド

ファームウェア:

```sh
cd firmware
pio run -e paper-mono
```

スマートフォンアプリ:

```sh
cd mobile
flutter pub get
flutter test
flutter run
```

iPhone版の実機ビルドには、NFC対応iPhone、NFC Tag Reading capability、
およびXcodeで各自設定するDeveloper Teamが必要です。署名情報は
リポジトリへコミットしません。

## デフォルト画像

`firmware/assets/default.jpg`を差し替えて再ビルドできます。PlatformIOの
プリビルド処理がJPEGを検証してアプリケーションバイナリへ埋め込むため、
初回起動用にLittleFSを別途書き込む必要はありません。

同梱デフォルト画像もMITライセンスです。詳細は
[`firmware/assets/LICENSE.md`](firmware/assets/LICENSE.md)を参照してください。

## ライセンス

本リポジトリのオリジナルのソフトウェア、ドキュメント、同梱デフォルト画像は
MITライセンスです。依存ライブラリにはそれぞれのライセンスが適用されます。
詳細は[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)と
[商用配布チェックリスト](docs/distribution_checklist.ja.md)を参照してください。

実装や共通挙動を変更する前に、[ビルド・書き込み手順](docs/building.ja.md)、
[製品仕様](docs/product_spec.ja.md)、[設計資料](docs/architecture.md)を
確認してください。IssueとPull Requestは元の共有ファームウェアではなく、
この独立リポジトリで受け付けます。開発方針は
[`CONTRIBUTING.md`](CONTRIBUTING.md)にまとめています。
