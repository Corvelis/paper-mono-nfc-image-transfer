# Paper Mono Image Sender

Android/iPhone共通のFlutterアプリです。画像を386 x 386へ切り抜いて白黒の
Baseline JPEGへ変換し、Paper Mono NFC Protocol v1でPaper Mono C153へ
送信します。画像送信時にはスマートフォンの時刻も自動同期します。画像なしで
時刻だけを同期することもできます。

通信実装はAndroidの`NfcA`とiOSのCore NFCをそれぞれネイティブ層に置き、
Flutter側は共通の画像編集UIと進捗表示を提供します。Wi-Fi、BLE、クラウド、
インターネット権限は使用しません。

通信仕様の正本はリポジトリ直下の
[`protocol/protocol_v1.md`](../protocol/protocol_v1.md)です。

## Run checks

```sh
flutter analyze
flutter test
flutter build apk --debug
flutter build ios --simulator --no-codesign
```

iOS実機ビルドでは、各開発者がXcodeで自分のTeamと署名を設定してください。
