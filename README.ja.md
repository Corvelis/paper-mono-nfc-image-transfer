# Paper Mono NFC Image Transfer

AndroidまたはiPhoneからM5Stack Paper MonoへNFCで画像を送り、時計、
カレンダー、歩数、変更可能な歩数目標、30日履歴と一緒に電子ペーパーへ
表示する非公式プロジェクトです。

対象はNFCを搭載する **M5Stack Paper Mono C153** です。NFCを搭載しない
Paper Mono Liteには対応しません。

Paper Mono固有部分を
[`Corvelis/stackchan-pet-fw`](https://github.com/Corvelis/stackchan-pet-fw)
から独立させ、共有のマルチデバイスファームウェアへ保守負担を持ち込まずに
開発できる構成にしています。

## 構成

- `firmware/`: Paper Mono C153専用PlatformIOファームウェア
- `mobile/`: Android/iPhone共通Flutterアプリ
- `protocol/`: NFC通信仕様と共通テストベクター
- `docs/`: 画面、操作、設計、書き込み、試験資料

Wi-Fi、BLE、クラウド、アカウント、解析、OTA更新は使用しません。

## 操作

通常画面でBtnAを約0.7秒長押しするとメニューを開きます。

```text
RECEIVE IMAGE    SYNC CLOCK
STEP GOAL        STEP HISTORY
RESET IMAGE      BACK
```

通常画面のBtnB短押しで省電力ロックへ入り、もう一度BtnBを押すと復帰します。
省電力中はフロントライト、タッチ、NFC、ジャイロを停止し、RTCと加速度を
使った歩数集計は継続します。

画像送信に失敗しても以前の画像は残ります。`RESET IMAGE`はNFC受信画像だけを
削除し、時計、歩数、履歴、目標設定を残したまま埋め込み画像へ戻します。

ビルドと実機への導入は[ビルド・書き込み手順](docs/building.ja.md)、詳細な挙動は
[製品仕様](docs/product_spec.ja.md)を参照してください。元の共有ファームウェアへ
影響を与えない独立プロジェクトとして、IssueやPull Requestもこのリポジトリ内で
完結させます。
