# 配布バイナリのインストール

この手順は、GitHub Releasesから入手したM5Stack Paper Mono C153用
ファームウェアとスマートフォンアプリをインストールする利用者向けの手順です。
ソースから開発・ビルドする場合は[ビルドと書き込み](building.ja.md)を参照してください。

## 1. Releaseを選ぶ

[最新のGitHub Release](https://github.com/Corvelis/paper-mono-nfc-image-transfer/releases/latest)
から、同じバージョン番号の必要なファイルをダウンロードします。

| ファイル | 用途 |
| --- | --- |
| `paper-mono-nfc-image-transfer-vX.Y.Z-full.bin` | 初回・完全再インストール用。0x0000へ書く単一ファームウェア |
| `paper-mono-nfc-image-transfer-vX.Y.Z-app.bin` | 保存データを維持する互換アップデート用。0x10000へ書く |
| `paper-mono-image-sender-vX.Y.Z-android.apk` | Androidへ直接インストールする署名済みアプリ |
| `paper-mono-image-sender-vX.Y.Z-android.aab` | Google Play提出用。端末へ直接インストールしない |
| `*-android-signing-certificate.txt` | Android署名証明書の公開フィンガープリント |
| `*-firmware-components.zip` | 分割書き込み・復旧・開発用 |
| `*-notices.zip` | プロジェクト・取得済み依存のライセンス表示と導入資料 |
| `release-manifest.json` | 対象機種、コミット、プロトコル等の情報 |
| `SHA256SUMS` | ダウンロード検証用SHA-256 |

初めてPaper Monoへ書く場合は`full.bin`、既存データを維持して互換バージョンへ
更新する場合は`app.bin`、Androidへ入れる場合は`.apk`を使います。どちらの
ファームウェアにもMITライセンスのデフォルト画像が埋め込まれているため、画像用の
ファイルシステムを別途書き込む必要はありません。Release Notesにパーティション
変更や完全再インストールの指定がある場合は、その指示を優先してください。

## 2. ダウンロードを検証する

`SHA256SUMS`内の該当ファイル名の値と、手元で計算した値が一致することを確認します。

macOS:

```sh
shasum -a 256 paper-mono-nfc-image-transfer-vX.Y.Z-full.bin
```

Linux:

```sh
sha256sum paper-mono-nfc-image-transfer-vX.Y.Z-full.bin
```

Windows PowerShell:

```powershell
Get-FileHash .\paper-mono-nfc-image-transfer-vX.Y.Z-full.bin -Algorithm SHA256
```

値が違う場合は、そのファイルを使用せず再ダウンロードしてください。

## 3. Paper Monoへ書き込む

対象はNFC内蔵の**M5Stack Paper Mono C153**だけです。Paper Mono Liteや
別のESP32製品には書き込まないでください。データ通信に対応したUSBケーブルと
Python 3を用意し、書き込みツールをインストールします。

```sh
python3 -m pip install "esptool==4.9.0"
```

1. Paper MonoとPCをUSB接続する。
2. 側面のリセットボタンを約2秒押し、側面LEDが点滅したら離してDownload Modeへ入る。
3. ポート名を確認する。macOSでは`/dev/cu.usbmodem...`、Linuxでは
   `/dev/ttyACM...`、Windowsでは`COM...`の形式が一般的です。
4. 初回・完全再インストールか、保存データを維持する更新かを選ぶ。

### 初回または完全再インストール

`<PORT>`とファイル名を置き換え、`full.bin`を0x0000へ書きます。

```sh
python3 -m esptool --chip esp32s3 --port <PORT> --baud 460800 \
  write_flash 0x0000 paper-mono-nfc-image-transfer-vX.Y.Z-full.bin
```

`full.bin`は結合イメージ内の空白も書くため、NVSにある歩数履歴、歩数目標、
タイムゾーン、受信画像の選択情報を初期化します。完全にまっさらな状態へ戻す場合は、
先に次を実行してLittleFS上の受信画像も含めて消去してから書き込みます。

```sh
python3 -m esptool --chip esp32s3 --port <PORT> erase_flash
```

### 保存データを維持する互換アップデート

現在もこのプロジェクトのファームウェアが動作していて、Release Notesに
パーティション変更の記載がない場合は、`app.bin`だけを0x10000へ書きます。

```sh
python3 -m esptool --chip esp32s3 --port <PORT> --baud 460800 \
  write_flash 0x10000 paper-mono-nfc-image-transfer-vX.Y.Z-app.bin
```

この方法はNVSとLittleFSを書き換えないため、受信画像ライブラリ、歩数履歴、歩数目標、
タイムゾーンを維持します。異なるプロジェクトからの移行、パーティション構成が
変わるRelease、起動できない状態からの復旧には使わず、`full.bin`を使用します。

書き込み完了後にリセットボタンを短く押します。画像、時計、カレンダー、歩数、
目標カウンターが表示されれば完了です。

M5Stack公式のDownload Mode手順は
[Paper Monoのプログラミング案内](https://docs.m5stack.com/en/arduino/papermono/program)
も参照してください。

## 4. Androidアプリを入れる

Android 7.0（API 24）以上でNFC-A対応の端末が必要です。

1. 同じReleaseの署名済み`android.apk`をAndroid端末へダウンロードする。
2. ブラウザまたはファイル管理アプリに対し、そのアプリからの「不明なアプリの
   インストール」を一時的に許可する。
3. APKを開いてインストールする。
4. 完了後、不要なら手順2の許可をオフへ戻す。

更新時は、同じApplication IDと同じ署名鍵のAPKだけが既存アプリを上書きできます。
署名が一致しないというエラーが出た場合、インストール元が異なる可能性があります。
アプリを削除するとアプリ側の設定も消えるため、先に入手元を確認してください。
各Releaseの`android-signing-certificate.txt`でも署名証明書のSHA-256を比較できます。
`.aab`はGoogle Playへ提出するためのファイルで、端末へ直接インストールするものでは
ありません。

## 5. iPhoneアプリを入れる

iOSアプリは端末ごとのApple署名が必要なので、未署名IPAをGitHub Releaseへは
公開しません。一般利用者向けはTestFlightまたはApp Storeで配布し、公開後は
Release NotesとREADMEに公式リンクを掲載します。それまではmacOS/Xcodeと
Apple Developer設定を用意し、[ソースから実機ビルド](building.ja.md#iphone)して
ください。

Appleの配布方式については
[Xcodeの配布ガイド](https://developer.apple.com/documentation/xcode/distributing-your-app-for-beta-testing-and-releases)
を参照してください。

## 6. 最初の接続

1. Paper Monoで`BtnA`を約0.7秒長押しする。
2. メニューから`RECEIVE IMAGE`を選ぶ。
3. アプリで`時計と表示`（DASH）または`全画面`（FULL）を選び、画像の切り抜きと
   モノクロプレビューを作成して送る。
4. 転送完了表示が出るまで、スマートフォンのNFCアンテナ位置をPaper Monoへ
   当てたままにする。

画像送信に時刻同期は必要ありません。時計を合わせる場合だけ、Paper Mono側で
`SYNC CLOCK`を開き、アプリの`NFCで時刻を同期`を実行します。

画像を初期状態へ戻すときはメニューの`RESET IMAGE`から`USE DEFAULT`を選びます。
受信画像の選択・削除は`IMAGE LIBRARY`から行えます。歩数履歴、目標、
時計設定は保持されます。通常画面の`BtnB`でフロントライト消灯の省電力ロックへ
入り、もう一度押すと復帰します。

各画面のボタン、タッチ、削除操作は[操作ガイド](usage.ja.md)を参照してください。

## トラブルシューティング

- ポートが出ない: 充電専用ではなくデータ対応USBケーブルを使い、Download Modeへ
  入り直す。
- 書き込みが接続待ちになる: 他のシリアルモニターを閉じ、ポート名を確認する。
- AndroidでAPKを更新できない: 既存アプリとRelease APKの署名元を確認する。
- NFCが始まらない: 実行する操作に合わせてPaper Mono側で`SYNC CLOCK`または
  `RECEIVE IMAGE`を開き、スマートフォンのNFCを有効にする。
- 転送が途切れる: 端末ごとに異なるNFCアンテナ位置を確認し、完了まで動かさない。
