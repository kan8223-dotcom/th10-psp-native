# th10_font32.bin（文字の表 / font table）

## 日本語

TH10 の文字は、ゲームデータ（th10.dat）と同じフォルダに置く `th10_font32.bin` から描きます。このリポジトリには表そのものは入っていません。次のどちらかで作ってください（Python 3 と Pillow が必要です）。

1. **MS ゴシック**（自分の Windows に入っているもの）

       python3 make_font_msgothic.py C:/Windows/Fonts/msgothic.ttc th10_font32.bin

   できた表は自分で使うためだけのものです。配布しないでください。

2. **Noto Sans CJK JP**（SIL Open Font License 1.1）

       python3 make_font_noto.py NotoSansJP-Regular.ttf th10_font32.bin

   入力として受け付けるのは、固定したファイル（Noto Sans CJK JP 2.004、4,491,696 バイト、SHA-256 `6ab1664d8adc20b19237ddc451c94e31f493cb851a1917242debf66f9af6da05`）だけです。入手先は TH08 PSP 移植の公開リポジトリで、コミットを固定してあります（URL は `make_font_noto.py` の先頭に書いてあります）。fontTools と、RAQM 付きの Pillow も必要です。
   字は MS ゴシックと同じ字幅（半角 16・全角 32）と行の位置で描くので、画面の配置は変わりません。できた表は Noto の改変版です。同じ場所にある `OFL.txt` と一緒であれば配布できます。

`charset_th10.bin` は、TH10 の文章が使う Shift_JIS の文字コードの一覧です。

## English

TH10 draws its text from `th10_font32.bin`, placed next to the game data (th10.dat). The table is not part of this repository; make it with one of these (Python 3 and Pillow):

1. **MS Gothic**, from your own Windows installation:

       python3 make_font_msgothic.py C:/Windows/Fonts/msgothic.ttc th10_font32.bin

   The result is for your own use only; do not redistribute it.

2. **Noto Sans CJK JP** (SIL Open Font License 1.1):

       python3 make_font_noto.py NotoSansJP-Regular.ttf th10_font32.bin

   Only the pinned file is accepted (Noto Sans CJK JP 2.004, 4,491,696 bytes, SHA-256 `6ab1664d8adc20b19237ddc451c94e31f493cb851a1917242debf66f9af6da05`), published at a fixed commit of the TH08 PSP port's public repository (the URL is at the top of `make_font_noto.py`). fontTools and Pillow with RAQM are also needed.
   The glyphs use MS Gothic's advances (16 for half-width, 32 for full-width) and line position, so the on-screen layout does not change. The table is a Modified Version of Noto and may be redistributed together with its `OFL.txt`.

`charset_th10.bin` lists the Shift_JIS codes TH10's texts use.
