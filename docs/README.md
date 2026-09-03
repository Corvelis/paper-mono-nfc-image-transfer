# ドキュメント

日本語 | [English](README.en.md)

日本語READMEをリポジトリの既定入口とし、このページから目的別の資料へ移動できます。

## 利用者向け

| 資料 | 内容 |
| --- | --- |
| [配布バイナリのインストール](install_binary.ja.md) | GitHub Releaseからファームウェアとアプリを導入する |
| [操作ガイド](usage.ja.md) | 物理ボタン、画像送信、時刻同期、Image Library、歩数、省電力 |
| [デモ動画](media/paper-mono-nfc-demo.mp4) | NFC画像送信とImage Libraryでの表示切替を映像で見る |

英語版は[Installation](install_binary.en.md)と[Operation guide](usage.en.md)です。

## 開発者向け

| 資料 | 内容 |
| --- | --- |
| [ビルドと書き込み](building.ja.md) | PlatformIO、Flutter、Android、iPhoneの開発ビルド |
| [製品仕様](product_spec.ja.md) | 対象範囲、UI、画像、時刻、歩数、省電力の要件 |
| [Architecture](architecture.md) | アプリ、NFC、保存、表示の責務分割 |
| [Protocol v1](../protocol/protocol_v1.md) | NFCワイヤ仕様の正本 |

通信を変更する場合は、`protocol/test_vectors.json`、ファームウェア、Android、iPhoneの
実装とテストを同時に更新します。UIまたは保存動作を変更する場合は、製品仕様と
日英操作ガイドも同じ変更に含めます。

## 配布担当向け

| 資料 | 内容 |
| --- | --- |
| [リリース手順](releasing.ja.md) | 署名、タグ、GitHub Release、ローカル梱包 |
| [配布時ライセンスチェックリスト](distribution_checklist.ja.md) | ソース、BIN、アプリ、デフォルト画像の確認事項 |

ライセンス本文と第三者表示は、ルートの[`LICENSE`](../LICENSE)と
[`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md)を参照してください。
