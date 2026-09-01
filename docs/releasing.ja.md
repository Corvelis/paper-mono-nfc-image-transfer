# リリース手順（メンテナー向け）

タグをpushすると`.github/workflows/release.yml`が、ファームウェア、署名済みAndroid
APK/AAB、ライセンス資料、マニフェスト、SHA-256一覧を作成し、GitHub Releaseへ
添付します。生成物は`dist/`へ置かれ、Gitへはコミットしません。

## 初回だけ行うAndroid署名設定

公開後の更新でも同じ鍵が必要です。鍵ファイルとパスワードを安全な場所へ複数バック
アップし、リポジトリへコミットしないでください。新規作成例:

```sh
keytool -genkeypair -v \
  -keystore paper-mono-image-sender-release.jks \
  -alias paper-mono-image-sender \
  -keyalg RSA -keysize 2048 -validity 10000
```

GitHubリポジトリのActions secretsへ次を登録します。

| Secret | 内容 |
| --- | --- |
| `ANDROID_KEYSTORE_BASE64` | JKSファイル全体をBase64化した文字列 |
| `ANDROID_KEYSTORE_PASSWORD` | Key store password |
| `ANDROID_KEY_ALIAS` | 上記例では`paper-mono-image-sender` |
| `ANDROID_KEY_PASSWORD` | Key password |

macOSでBase64文字列をクリップボードへ入れる例:

```sh
base64 -i paper-mono-image-sender-release.jks | pbcopy
```

Google Playでも同じApplication IDのアプリを配布する場合、GitHub配布APKと
Google Play版が互いに更新可能かはアプリ署名鍵で決まります。Play App Signingへ
登録する前に、直接配布とPlay配布で使うアプリ署名鍵の方針を固定してください。
Android公式の[アプリ署名](https://developer.android.com/studio/publish/app-signing)
も確認します。

## リリース作成

1. `VERSION`と`mobile/pubspec.yaml`のバージョン名を同じ`X.Y.Z`へ更新する。
2. Androidの`+build`番号はローカルの基準値として増やす。Release Actionsでは
   単調増加するGitHub run numberを最終versionCodeとして使用する。
3. 日英README、導入資料、ライセンス監査、Release Notesに必要な変更点を更新する。
4. ファームウェア実機、Android実機、iPhone実機で主要シナリオを確認する。
5. main上の対象コミットへ、できれば署名付きタグを作成してpushする。

```sh
bash scripts/check_version.sh
git tag -s v0.1.0 -m "Paper Mono NFC Image Transfer v0.1.0"
git push origin v0.1.0
```

Actionsはタグの`vX.Y.Z`と`VERSION`が一致しない場合や、署名Secretが不足している
場合に停止します。完了後はReleaseから全資産をダウンロードし、`SHA256SUMS`、
APKの署名、Paper Monoへの`full.bin`書き込みを再確認します。特に
`android-signing-certificate.txt`のSHA-256証明書値が前回Releaseと同じであることを
確認します。以前のバージョンで受信画像・歩数履歴・目標・タイムゾーンを設定した
実機へ`app.bin`を0x10000から書き、すべて維持されることも更新テストへ含めます。

## ローカルでの梱包確認

ファームウェア:

```sh
PLATFORMIO_CMD=pio bash scripts/package_firmware.sh
```

Androidは公開用鍵の環境変数を設定した場合だけ生成できます。

```sh
export ANDROID_KEYSTORE_PATH=/安全な場所/release.jks
export ANDROID_KEYSTORE_PASSWORD='...'
export ANDROID_KEY_ALIAS='...'
export ANDROID_KEY_PASSWORD='...'
bash scripts/package_android.sh 0.1.0 1
```

最後に資料、マニフェスト、SHA-256一覧を追加します。

```sh
bash scripts/finalize_release.sh
```

## iPhone配布

GitHub Actionsではシミュレータ用のビルド検証だけを行い、IPAはGitHub Releaseへ
添付しません。一般配布はApp Store ConnectでArchiveをTestFlightまたはApp Storeへ
提出します。Ad Hoc配布は登録済み端末に限定されるため、一般向けRelease資産の代用には
しません。公開URLが確定したらREADMEと各Release Notesへ追加します。
