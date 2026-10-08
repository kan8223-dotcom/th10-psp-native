# 統合 EBOOT（機種判定ランチャー＋初回の XMB 素材生成）

2026-10-07〜08。妖々夢（TH07）の `psp/unified_launcher` と `tools/pack_unified_pbp.py`・`audit_unified_pbp.py` を流用し、風神録向けに直した。SND0（XMB のタイトル曲）は TH07 に無く、ここで追加した。

## 構成

- `native/psp_launcher/`：ランチャー（`main.cpp` 起動と LoadExec、`model_dispatch.cpp` 機種判定、`xmb_selfwrap.cpp` XMB 素材の生成と固定枠への書き込み、`xmb_sound.cpp` SND0 のエンコード、`atracdenc.mk`）。
- `native/third_party/atracdenc/`：ATRAC3 エンコーダ（LGPL-2.1、出典と改変は同フォルダの README）。
- `tools/unified/pack_unified_pbp.py`・`audit_unified_pbp.py`：詰める・監査する。
- `native/psp_launcher/host_check/`：`xmb_selfwrap.cpp`・`xmb_sound.cpp` を PC でビルドして、生成と書き込みを試す（`build.sh <libpng 1.6 のソース> <出力>`、`selfwrap_check <ms0: に見立てるフォルダ> <ms0:/.../EBOOT.PBP>`）。
- `native/psp_launcher/sound_check/`：SND0 のエンコードだけを PSP 上で試す（配布しない）。

## 動き

1. `kuKernelGetModel`：0 → PSP-1000 用、1〜7 → PSP-2000 以降用、それ以外は安全側の 1000 用。
2. DATA.PSAR（`TH10UP02` v2：1000 用 PBP・2000+ 用 PBP・GE4 補助 PRX）から選んだ方を `TH10RUNTIME.PBP` に、2000+ なら `ge4wrap_texv1.prx` も書き出す（CRC が合えば再利用）。風神録の GE4 補助は TH07 とバイト一致（sha256 3dc5c753…）。
3. XMB 素材：データは、ランタイムと同じ場所だけを探す（`th10run.txt` の `--data`、無ければ EBOOT のフォルダ）。東方風神録 v1.00a の `th10.dat`（27,696,219 B、THA1）と `thbgm.dat`（403,789,620 B、ZWAV）がそろったときだけ生成する。
   - ICON0（144×80）：`title.anm` の `title/title_logo.png` の最初のスプライト（512×128。テクスチャ下段の小さい複製は除く）。
   - PIC1（480×272）：`title/title00a.png`＋`title00b.png` をつないだ 640×480 のタイトル背景（上から 45% の位置で切る）に、左上ロゴ＋影。
   - SND0（ATRAC3 LP2 132 kbps）：`thbgm.fmt` の `th10_02.wav`（タイトル画面の曲、platform/Title.cpp:72）の頭から 27 秒。20 ms のフェードイン、3 秒のフェードアウト。atracdenc の軽量モード（トーン成分・ゲイン制御なし）。生成中はクロックを 333 MHz にする。
   - 書き込みは、固定枠（ICON0 64 KiB・PIC1 512 KiB・SND0 480 KiB）の中だけ。PBP の表・DATA.PSP・DATA.PSAR は不変。3 枠とも目印（PNG は `thSb` チャンク、RIFF は data の後ろの `thSb` チャンク。`TH10PLN3` はプレースホルダ、`TH10XMB3` は生成済み、役割 I/P/S）を確かめ、3 枠そろって初めて「生成済み」とする。配布時は、透明 PNG 2 枚と 1 秒の無音が入っている。
4. LoadExec：実機は `sctrlKernelLoadExecVSHMs2`（ef0 は Ef2）。PPSSPP には SystemCtrlForUser が無いので、`ms0:/PSP/SYSTEM/ppsspp.ini` があれば `sceKernelLoadExec`。
5. ランタイム（`45_ugo`＝44_play、`45_u1k`＝42_rel40 と同じフラグ）は、起動時に `sceIoChdir(EBOOT のフォルダ)` を行う（TH07 psp/fileio.cpp:693 の記録どおり）。`th10run.txt` が無ければ、遊ぶ設定（`--ticks 0 --frameskip 1 --hash 0 --trace none --progress 0`）で起動する。

## 検証（2026-10-08、PC と PPSSPP まで。実機は未確認）

- PC ハーネス：生成→書き込み→2 回目は何もしない。枠の外はバイト不変。データ無し・th10.dat が 1 バイト短い・`--dump-entry` が次の語を食う、の 3 例では何も書かない。
- Linux の PPSSPPHeadless（TH07 PRAM 用に 9/9 ビルドしたもの。機種は 1 固定）：MIPS で 3 枠を生成して書き込み、PC の生成物とバイト一致。模擬時計で、全体 17.4 秒、SND0 エンコード 14.9 秒。
- Windows の PPSSPP 1.20.4：機種 1 → r41a、機種 0 → r41b。デモ 6000・1 面 3000 の状態（列 1〜11）は PC と一致（ランタイム差し替え後）。起動した EBOOT は PPSSPP が共有書き込み不可で開いたままなので、書き込みは拒否され（ヘルパー経由でも -7）、ゲームはそのまま起動する。
- SND0 の音質：ffmpeg でデコードし、極性反転と 1162 サンプルの遅延を合わせると SNR 20.3 dB・相関 0.996（atracdenc→ffmpeg の経路で極性が反転する。耳では区別できない）。

## まだ実機でしか分からないこと

- SND0 エンコードの実時間（PPSSPP の模擬時計は 14.9 秒。実機はこれより長いかもしれない）。
- XMB がアイコン・背景・曲を出すか（SND0 の約 500 KB 上限は言い伝えで、未確認）。
- `sctrlKernelLoadExecVSHMs2` で起動したランタイムが、2000+ で拡張メモリを得るか（TH07 は ARK-5 の Use Extra Memory = Max を必須とした）。

## ランチャーの MEMSIZE

ランチャーの PARAM.SFO は MEMSIZE=1（TH07 は 0）。PPSSPP は、起動した PBP の MEMSIZE でユーザーメモリの大きさを一度だけ決め、LoadExec の後は見直さない（PSPLoaders.cpp UseLargeMem）。0 のままだと、Go 用ランタイムが PPSSPP で 24 MB しか得られず、tick 2 で確保に失敗した。風神録のランタイム PBP は、1000 用・Go 用とも元から MEMSIZE=1。

## 実機（Go、2026-10-08）

1 回目（1027c3ce）：機種 4 → Go 用。生成は全体 29.1 秒、うち SND0 のエンコード 24.6 秒（333 MHz）。`sctrlKernelLoadExecVSHMs2` で起動したランタイムの room_start は 48.4 MB（XMB から直接起動した 43_go と同じ値）、vram 4 MiB（chdir で GE4 補助を読めた）。XMB の ICON0・PIC1 は出たが、曲は鳴らなかった。

SND0 の手直し：POP-FE（sahlberg/pop-fe の riff.py、XMB 用 SND0.AT3 を atracdenc で作るツール。PSP の上限は 500,000 B・59 秒としている）のヘッダに合わせた。1 回目は fact が「全サンプル数, 1024」で smpl が無く、デコードできる長さより長く申告していた。現在は「fmt → fact（最後の 4096 サンプル境界から 0x2000 手前で終わり、0x800 から始まる）→ smpl（その範囲を 1 ループ）→ data → 目印チャンク」の順。data の後ろのチャンクは POP-FE も LIST を置いている。あわせて、生成中に進捗バー（40 マス、%、経過秒）を出すようにした。2 回目（a65b546f）を TH10T に投入済み。

2 回目（a65b546f）：進捗バーが出て、XMB で曲が鳴った（目視、2026-10-08）。SND0 で 1 回目から変えたのはヘッダ（fact と smpl）だけなので、鳴らなかった原因はヘッダ。Go で残る課題は無い。PSP-1000（機種 0、データは EBOOT と同じフォルダ）は未確認。

## 実機（PSP-1000、2026-10-08）

a65b546f を 1000 の TH10T（正式版 40 と入れ替え、データはフォルダ内、th10run.txt に --data なし）へ。機種 0 → 1000 用（GE4 補助は書き出さない）、データは EBOOT のフォルダから見つけた。生成は全体 26.7 秒、うち SND0 のエンコード 21.4 秒（API 333 MHz、ランタイムの実測は 459 MHz）。ランタイム r41b の room_start は 21.23 MB（正式版 40 は 21.24 MB）、確保失敗 0、最大使用 17.6 MB。XMB に絵と曲が出た（目視）。これで Go・1000 の両方で合格。
