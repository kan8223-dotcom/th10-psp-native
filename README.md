**日本語** | [English](README_EN.md)

# 東方風神録 PSP（th10-psp-native）

東方風神録 ～ Mountain of Faith. を PSP で動かす、非公式の移植です。エミュレーションではありません。上流の C++ による再実装（[YomotsuHisami/th10](https://github.com/YomotsuHisami/th10)）を、PSP の CPU と GE で動かしています。

1 本の EBOOT で、PSP-1000 と PSP-2000 / 3000 / Go の両方に対応します。起動時に機種を調べ、それぞれ用の本体を起動します。

> 本プロジェクトは GensokyoClub とは無関係です（GensokyoClub のデコンパイルは使っていません）。原作者・上流・参照先には、このプロジェクトについて問い合わせないでください。

## 必要なもの

- ARK-5 を入れた PSP（PSP-1000 / 2000 / 3000 / Go）。実機での確認は PSP Go と PSP-1000（どちらも ARK-5）。
- PSP-2000 / 3000 / Go では、ARK-5 の設定でこのゲームの「Use Extra Memory」を「Max」にしてください。「Default」や「Off」のままだと、起動しないおそれがあります（PSP-1000 には関係ありません）。
- 東方風神録 **v1.00a** の原作データ（ご自身で正規に入手したもの）
  - `th10.dat`（27,696,219 バイト）
  - `thbgm.dat`（403,789,620 バイト）

大きさが違う版のデータは受け付けません。

## 入れ方

1. リリースの ZIP を展開し、中の `TH10PSP` フォルダを、メモリースティックの `PSP/GAME/` にコピーする。
2. 同じフォルダ（`PSP/GAME/TH10PSP/`）に `th10.dat` と `thbgm.dat` を置く。
3. XMB の「ゲーム」に、アイコンもタイトルも無い項目ができるので、それを起動する。そのあと XMB に戻ると、アイコン・背景・曲ができています。

アイコン・背景・曲（タイトル画面の曲の頭 27 秒）は、初回の起動のときだけ原作データから作り、EBOOT に書き込みます。30 秒ほどかかり、その間は進捗バーが出ます。電源を切らないでください。

配布物には、原作から作った画像や音は入っていません（透明の絵と無音だけ）。書き込みのあとの EBOOT は、あなたのデータから作った個人用のものです。再配布しないでください。

セーブとリプレイは、同じフォルダの `save/` に入ります。

## 操作

| PSP | ゲーム |
|---|---|
| 十字キー・アナログ | 移動 |
| × | ショット・決定 |
| ○ | ボム・キャンセル |
| □ / L / R | 低速 |
| △ | 会話送り |
| START | ポーズ |
| SELECT | 録画の開始と停止（PSP-2000/3000/Go のみ。`ms0:/VIDEO/TH10/` に AVI） |

## 文字の表（th10_font32.bin）

ゲーム中の文字は、EBOOT と同じフォルダの `th10_font32.bin` から描きます。配布物に入っている表は Noto Sans JP（SIL Open Font License 1.1）から作ったもので、そのまま使えます。

原作と同じ MS ゴシックの字にしたい場合は、Windows の `C:\Windows\Fonts\msgothic.ttc` を `th10.dat` と同じフォルダにコピーしてから起動してください。起動したときに MS ゴシックから表を作り、`th10_font32.bin` を置き換えます（PSP-1000 で約 2 秒、進捗バーが出ます）。同梱の Noto 版は `th10_font32_noto.bin` という名前で残ります。

- 新しい版に入れ替えて `th10_font32.bin` が Noto 版に戻っても、`msgothic.ttc` を置いたままなら次の起動で作り直します。
- Noto 版に戻すときは、`msgothic.ttc` と `th10_font32.bin` を消し、`th10_font32_noto.bin` の名前を `th10_font32.bin` に変えます。
- MS ゴシックでないファイルを `msgothic.ttc` という名前で置くと、起動のたびに理由を 4 秒表示して、今の表のまま進みます。
- `msgothic.ttc` と、そこから作った表は個人用です。再配布しないでください。
- PC で作ることもできます（[tools/fonts/README.md](tools/fonts/README.md)。Python が必要です）。Pillow 12.1.1 なら、PSP で作った表と同じものができます。

## 実機での数字

ゲームの計算は毎秒 60 tick、描画は 30 fps（2 tick に 1 回）です。下の「処理落ちの割合」は、描画 1 回分の 33.3 ms に「2 tick の計算と描画」が収まらなかった割合です。

| 項目 | PSP Go | PSP-1000 | 条件 |
|---|---|---|---|
| 処理落ちの割合 | 14.51% | 14.06% | Go：リプレイ再生の 4〜6 面。1000：実プレイの 4 面（2026-10-07） |
| 起動時の空きメモリ | 48.4 MB | 21.2 MB | 本体の起動直後 |
| 使ったメモリの最大 | 34.2 MB | 17.6 MB | Go：4〜6 面、1000：6 面 |
| 初回の XMB 素材の生成 | 29.1 秒 | 26.7 秒 | 2026-10-08 |
| CPU の実クロック | 418 MHz | 459 MHz | ARK-5 のクロック設定による（ゲーム側の設定は 333 MHz） |

測り方や、試して効かなかった手は、[docs/](docs/) にまとめています。

## ソースからのビルド

PSPDEV ツールチェーン（`/usr/local/pspdev`）と python3 が必要です。

    tools/release/build_release.sh

`dist/EBOOT.PBP` ができます。中身は、2 本の本体（PSP-1000 用と PSP-2000/3000/Go 用）、起動時に機種を判定するランチャー、GE4 補助 PRX です。原作データが混ざっていないかの検査まで、このスクリプトで行います。

PC 上の確認用のビルド（ゲームの状態をトレースに書き出す）は `native/CMakeLists.txt` です。

## 開発について

コードは AI が書き、自分は指示と数日の猛烈なデバッグを担当しました。成果の大半は、上流の再実装（YomotsuHisami/th10）のものです。

## 謝辞

<a href="https://github.com/YomotsuHisami"><img src="https://github.com/YomotsuHisami.png" width="64" alt="YomotsuHisami"></a>
<a href="https://github.com/saekaze"><img src="https://github.com/saekaze.png" width="64" alt="Saekaze"></a>

- 上海アリス幻樂団 / ZUN — 原作
- YomotsuHisami 氏 — 風神録の再実装（[YomotsuHisami/th10](https://github.com/YomotsuHisami/th10)）
- 冴風（Saekaze）さん — 風神録の Switch 移植（th10-switch）。この移植のきっかけと参考になりました。
- M-cid（m-c/d）氏 — PSP Media Engine Custom Core
- Daniil Cherednik 氏 — atracdenc（XMB の曲のエンコード）
- The FreeType Project — FreeType（PSP 上で MS ゴシックから文字の表を作る）
- Adobe / Google — Noto Sans CJK JP（同梱の文字の表の元）

## ライセンス

自分で書いた部分は MIT License です（[LICENSE](LICENSE)）。上流の再実装と同梱物は、それぞれの条件に従います（[THIRD_PARTY.md](THIRD_PARTY.md)）。文字の表 `th10_font32.bin` は Noto Sans JP の改変版で、MIT ではなく SIL Open Font License 1.1 に従います（[licenses/NotoSansJP/](licenses/NotoSansJP/)）。原作の著作権は上海アリス幻樂団にあります。
