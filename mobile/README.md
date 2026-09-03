# Paper Mono Image Sender

Android/iPhone共通のFlutterアプリです。画像を386 x 386へ切り抜いて白黒の
Baseline JPEGへ変換するDASHモードと、480 x 800へ変換するFULLモードを選び、
Paper Mono NFC Protocol v1でPaper Mono C153へ送信します。画像送信と時刻同期は
独立しており、画像送信時に時刻を変更しません。画像なしで時刻だけを同期できます。

通信実装はAndroidの`NfcA`とiOSのCore NFCをそれぞれネイティブ層に置き、
Flutter側は共通の画像編集UIと進捗表示を提供します。Wi-Fi、BLE、クラウド、
インターネット権限は使用しません。

右上の言語アイコンから`日本語`と`English`を切り替えられます。選択は端末内に
保存され、次回起動時も維持されます。初回は日本語です。Flutter画面、NFC進捗・
エラー、iPhoneのCore NFCシートを選択言語へ揃えます。

## アプリの流れ

1. `時計と表示`または`全画面`を選ぶ。
2. ギャラリーまたはカメラから画像を選ぶ。
3. 切り抜きを調整し、モノクロの送信プレビューを生成する。
4. Paper Mono側で`RECEIVE IMAGE`を開き、`NFCで送信`を押す。
5. 完了表示までスマートフォンのNFCアンテナをPaper Monoへ当てたままにする。

画像名はプロトコルへ含めません。未完了の送信画像、モード、CRC、Transfer IDは
アプリ内へ一時保存し、NFCが途切れた場合は同じ画像の送信を再開できます。
Paper Monoが保存完了を返した時点で、この一時データを削除します。
保存完了または時刻同期完了後は進捗バーを停止し、完了表示を約2秒後に自動で閉じます。
エラー表示は原因を確認できるよう、手動で閉じるまで残ります。

時刻を合わせる場合は、Paper Mono側で`SYNC CLOCK`を開き、アプリ上部の
`NFCで時刻を同期`を使用します。画像を送る前後どちらでも実行できます。

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
