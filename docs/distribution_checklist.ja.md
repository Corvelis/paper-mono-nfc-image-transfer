# 配布時ライセンスチェックリスト

この文書は、現在固定されている依存関係を前提にした実務上の確認項目です。
法的助言ではありません。依存バージョンを変更した場合は、各リリース時点で
ライセンスを再確認してください。

## GitHubでソースを公開する場合

- ルートの`LICENSE`（プロジェクトコードのMIT）を残す。
- `firmware/assets/LICENSE.md`（デフォルト画像のCC BY 4.0）を残す。
- `THIRD_PARTY_NOTICES.md`と依存バージョンのロックファイルを残す。
- M5Unit-NFCへビルド時パッチを当てている事実を削除しない。
- デフォルト画像を差し替える場合は、新しい画像の権利者とライセンスも更新する。

ソースを取得して各自が自分の端末向けにビルドするだけなら、これが基本形です。

## ファームウェアの`.bin`または書き込み済み製品を配布する場合

Arduino-ESP32 2.0.17はLGPL-2.1-or-laterです。MITライセンスのM5系
ライブラリとは配布条件が異なるため、次をリリース作業に含めます。

- 使用したArduino-ESP32の完全なライセンス文と著作権表示を添付する。
- 配布したバイナリに対応する、同一タグのプロジェクトソース、ビルドスクリプト、
  依存バージョン、Arduino-ESP32、同梱ESP-IDFコンポーネント、および各第三者
  コンポーネントの対応ソース・ライセンス表示を入手可能にする。
- 利用者がArduino-ESP32を変更して再ビルドまたは再リンクできる形を維持する。
- 利用規約やEULAで、その変更のデバッグに必要なリバースエンジニアリングまで
  一律禁止しない。
- M5Stack各ライブラリ、ArduinoJsonなどのMIT著作権・許諾表示も添付する。
- 対応ソースをURLで案内する場合は、製品の配布期間だけでなく、必要な期間に
  わたり確実に取得できるようリリース資産を保存する。

このリポジトリを公開したまま、ファームウェアのGitタグと配布バイナリを1対1で
対応させる運用が最も管理しやすいです。販売製品では、製品説明書や同梱文書にも
「オープンソースライセンス」と対応ソースのURLを記載してください。

PlatformIO Core自体はApache-2.0ですが、書き込みに使うesptoolは
GPL-2.0-or-laterです。esptoolは生成ファームウェアへリンクされないため、通常の
ビルドや書き込みだけでファームウェアがGPLになることはありません。ただし、
セットアップツール等へesptool本体を同梱して配る場合は、そのGPL対応が別途必要です。

## Android/iPhoneアプリを配布する場合

- `mobile/pubspec.lock`と`mobile/ios/Podfile.lock`をリリース毎に固定する。
- アプリ右上の情報アイコンから、Flutterが収集したライセンス一覧を確認する。
- 最終APK/AABに`flutter_assets/NOTICES.Z`が入っていることを確認する。
- Androidの最終依存ツリーに対して、AndroidX、Commons IOなどの表示を確認する。
- iOSのCocoaPods acknowledgementsに、SDWebImage、SDWebImageWebPCoder、
  libwebpが含まれることを確認し、アプリ内または配布資料から閲覧可能にする。
- Android SDK License Agreement、Google Playの契約、Apple Developer Program
  License Agreement、App Store Connectの契約を配布主体のアカウントで受諾する。
- 有料アプリ、アプリ内課金、企業内配布を行う場合は、それぞれ追加契約も確認する。

## デフォルト画像を含む製品を配布する場合

`firmware/assets/default.jpg`はCC BY 4.0で商用利用できます。画像の上にクレジットを
重ねる必要はありませんが、説明書、アプリのライセンス画面、製品ページ、または
リリースノート等の合理的な場所に、次のような表示を入れます。

> Paper Mono default artwork © 2026 あいろぐ, licensed under CC BY 4.0.

画像を加工した場合は、加工したことも明記し、CC BY 4.0のURLを併記します。
