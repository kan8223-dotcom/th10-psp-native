# th10_font32.bin（文字の表 / font table）

## 日本語

TH10 の文字は、EBOOT と同じフォルダに置く `th10_font32.bin` から描きます。リリースの ZIP には、Noto Sans CJK JP（SIL Open Font License 1.1）から作った表が `th10_font32.bin` として入っているので、ふつうはそれを使うだけです。このリポジトリでは `th10_font32_noto.bin` がその表です（作り方・元のフォント・ハッシュは `licenses/NotoSansJP/FONTLOG-TH10PSP.txt`）。

MS ゴシックの表は、PSP の上でも作れます。`C:\Windows\Fonts\msgothic.ttc` を `th10.dat` と同じフォルダに置いて起動すると、ランチャーが作って置き換えます（`native/psp_launcher/text_table.cpp`。FreeType 2.14.1 を同梱していて、Pillow 12.1.1（中の FreeType が 2.14.1）で動かした下の `make_font_msgothic.py` と同じバイト列になります）。

ここのスクリプトは、PC で表を作るときに使います（Python 3 と Pillow が必要です）。

1. **MS ゴシック**（自分の Windows に入っているもの。原作と同じ字になります）

       python3 make_font_msgothic.py C:/Windows/Fonts/msgothic.ttc th10_font32.bin

   できた表を、EBOOT と同じフォルダの `th10_font32.bin` と置き換えます。自分で使うためだけのものです。配布しないでください。

2. **Noto Sans CJK JP**（リリースの表を作り直して確かめるとき）

       python3 make_font_noto.py NotoSansJP-Regular.ttf th10_font32.bin

   入力として受け付けるのは、固定したファイル（Noto Sans CJK JP 2.004、4,491,696 バイト、SHA-256 `6ab1664d8adc20b19237ddc451c94e31f493cb851a1917242debf66f9af6da05`）だけです。入手先は TH08 PSP 移植の公開リポジトリで、コミットを固定してあります（URL は `make_font_noto.py` の先頭に書いてあります）。fontTools と、RAQM 付きの Pillow も必要です。
   字は MS ゴシックと同じ字幅（半角 16・全角 32）と行の位置で描くので、画面の配置は変わりません。Pillow や FreeType の版が違うと、一部の画素が `th10_font32_noto.bin` と違うことがあります。リリースに入れるのは `th10_font32_noto.bin` のバイト列です。

`charset_th10.bin` は、TH10 の文章が使う Shift_JIS の文字コードの一覧です。`gen_charset_unicode.py` は、それを cp932 で Unicode にした表（`native/psp_launcher/charset_th10_unicode.inc`、ランチャー用）を書き出します。

## English

TH10 draws its text from `th10_font32.bin`, placed next to the EBOOT. The release ZIP includes a table made from Noto Sans CJK JP (SIL Open Font License 1.1) under that name, so normally there is nothing to make. In this repository that table is `th10_font32_noto.bin` (its source font, tools and hash: `licenses/NotoSansJP/FONTLOG-TH10PSP.txt`).

The MS Gothic table can also be made on the PSP: put `C:\Windows\Fonts\msgothic.ttc` next to `th10.dat` and start the game, and the launcher makes it and puts it in place (`native/psp_launcher/text_table.cpp`; it carries FreeType 2.14.1 and writes the same bytes as `make_font_msgothic.py` below run with Pillow 12.1.1, whose FreeType is 2.14.1).

The scripts here make a table on a PC (Python 3 and Pillow):

1. **MS Gothic**, from your own Windows installation (the original game's look):

       python3 make_font_msgothic.py C:/Windows/Fonts/msgothic.ttc th10_font32.bin

   Put the result in place of `th10_font32.bin` next to the EBOOT. It is for your own use only; do not redistribute it.

2. **Noto Sans CJK JP**, to rebuild and check the release's table:

       python3 make_font_noto.py NotoSansJP-Regular.ttf th10_font32.bin

   Only the pinned file is accepted (Noto Sans CJK JP 2.004, 4,491,696 bytes, SHA-256 `6ab1664d8adc20b19237ddc451c94e31f493cb851a1917242debf66f9af6da05`), published at a fixed commit of the TH08 PSP port's public repository (the URL is at the top of `make_font_noto.py`). fontTools and Pillow with RAQM are also needed.
   The glyphs use MS Gothic's advances (16 for half-width, 32 for full-width) and line position, so the on-screen layout does not change. Other Pillow or FreeType versions may give some pixels that differ from `th10_font32_noto.bin`; the release ships the bytes of `th10_font32_noto.bin`.

`charset_th10.bin` lists the Shift_JIS codes TH10's texts use. `gen_charset_unicode.py` writes them decoded with cp932 (`native/psp_launcher/charset_th10_unicode.inc`, for the launcher).
