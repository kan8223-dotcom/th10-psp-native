# Third-party components and notices

このリポジトリで kan82 が書いた部分は MIT License（`LICENSE`）です。それ以外の部分は、それぞれ下記の条件に従います。
The parts of this repository written by kan82 are under the MIT License (`LICENSE`).
Everything listed below keeps its own terms; the root `LICENSE` does not relicense it.

## Original game (not included)

- 東方風神録 ～ Mountain of Faith. © 上海アリス幻樂団 / ZUN (Team Shanghai Alice).
  No executable, data, music, replay or save file of the original game is in this
  repository or in its releases. You need your own copy of version 1.00a.
  `tools/unified/check_no_original_assets.py` rejects original data, personal data and
  locally generated XMB media.

## Game engine: TH10 reimplementation

- [YomotsuHisami/th10](https://github.com/YomotsuHisami/th10), branch `portable`.
  `th10_web/cpp/` and the files taken from `portable/` come from upstream commit
  `0074e58` (2026-09-16) and are modified by this port.
- Upstream declares the MIT License in its README, at commit
  [`f5df61cfeab42015e0a6c5894c8d836d079ec1a3`](https://github.com/YomotsuHisami/th10/commit/f5df61cfeab42015e0a6c5894c8d836d079ec1a3)
  (2026-10-05), section "License":
  > This project is licensed under the MIT License.

  The upstream repository has no separate LICENSE file at that commit. Credits for
  the reimplementation are those of the upstream README.
- Upstream's own third-party notices are kept in
  `th10_web/assets/vendor/th10-rebuilt.LICENSE`.

## Compiled into the PSP binaries

| Component | Where | License | Notes |
|---|---|---|---|
| Berkeley SoftFloat 3e (John R. Hauser, The Regents of the University of California) | `th10_web/cpp/rebuild/third_party/softfloat.c` | BSD-3-Clause | notices in the file and in `th10-rebuilt.LICENSE` |
| PSP Media Engine Custom Core (MECC) by m-c/d | `native/psp/third_party/me-custom-core/` | MIT (`LICENSE.md` there) | from [mcidclan/psp-media-engine-custom-core](https://github.com/mcidclan/psp-media-engine-custom-core) (around `7dbf492`, 2026-08-20), carried and modified by the author's TH07/TH08 PSP ports; not identical to any upstream commit |
| atracdenc (Daniil Cherednik) — ATRAC3 encoder subset | `native/third_party/atracdenc/` | LGPL-2.1-or-later (`LICENSE` there) | upstream commit and the three local changes are in `native/third_party/atracdenc/README.md`; statically linked into the launcher, whose complete source is this repository, so it can be rebuilt and relinked |
| FreeType 2.14.1 (The FreeType Project) — TrueType driver, SFNT tables, smooth renderer | `native/third_party/freetype/` | FreeType License (FTL), chosen from FTL / GPL v2 (`docs/FTL.TXT` there) | statically linked into the launcher, which makes the text table from the user's own MS Gothic; local options in `native/third_party/freetype/README.md`. Portions of this software are copyright © 2025 The FreeType Project (https://freetype.org). All rights reserved. |
| KISS FFT (Mark Borgerding) | `native/third_party/atracdenc/src/lib/fft/kissfft_impl/` | BSD-3-Clause | part of the atracdenc subset |
| FFmpeg ATRAC3plus tables (Maxim Poliakovski) | `native/third_party/atracdenc/src/atrac/at3p/ff/atrac3plus_data.h` | LGPL-2.1-or-later | as shipped by atracdenc |
| libpng | PSPDEV toolchain (`-lpng16`, launcher) | PNG Reference Library License v2 | text in `licenses/libpng.txt` |
| zlib | PSPDEV toolchain (`-lz`, launcher) | zlib License | text in `licenses/zlib.txt` |
| PSPSDK | PSPDEV toolchain (all binaries) | BSD-style | text in `licenses/pspsdk.txt` |
| newlib | PSPDEV toolchain (all binaries) | BSD-style and similar | text in `licenses/newlib.txt` |
| GE4 wrapper `ge4wrap_texv1.prx` | `native/psp/ge4wrap_texv1.prx`, source in `native/psp/ge4wrap/` | MIT (the author's own TH07 code) | rebuilt from that source with pinned SHA-256 |

The CFW imports (`native/psp_cfw/`) are this project's own declarations made with
PSPSDK's `pspimport.s` macros; no file of a CFW SDK is used.

## Methods and formats followed (no code copied)

- Cephes Math Library (Stephen L. Moshier): the sin/cos polynomial coefficients and
  the argument reduction in `portable/numeric/RenderTrig.hpp` follow Cephes `sinf`/`cosf`.
- Independent JPEG Group: `native/psp/RecJpeg.hpp` (the recorder's MJPEG encoder) is
  a rewrite of algorithms of IJG libjpeg (fast integer DCT, quantization and the
  standard Huffman tables of ITU-T T.81 Annex K). This software is based in part on
  the work of the Independent JPEG Group.
- POP-FE ([sahlberg/pop-fe](https://github.com/sahlberg/pop-fe)): the SND0.AT3 header
  (`fact` end and start, one `smpl` loop) follows the values its `riff.py` writes for
  XMB music.

## Text table (included)

- `th10_font32.bin` in the release (`tools/fonts/th10_font32_noto.bin` in this
  repository) is made by `tools/fonts/make_font_noto.py` from Noto Sans CJK JP 2.004
  Regular, Copyright 2014-2021 Adobe, with Reserved Font Name 'Source'. It is a
  Modified Version of that font and is under the SIL Open Font License 1.1 only; the
  root `LICENSE` (MIT) does not apply to it. License text:
  `licenses/NotoSansJP/OFL.txt`; source file, tools and changes:
  `licenses/NotoSansJP/FONTLOG-TH10PSP.txt`.
- A table made from MS Gothic of the user's own Windows, on the PSP by the launcher
  (from `msgothic.ttc` placed next to the game data) or on a PC with
  `tools/fonts/make_font_msgothic.py`, is for personal use only: neither `msgothic.ttc`
  nor such a table is in this repository or the releases, and they must not be
  redistributed. `tools/unified/check_no_original_assets.py` accepts only the Noto
  table, by its SHA-256, and rejects `msgothic.ttc` and any other `.ttc`.

## Testing tools (not distributed)

PPSSPP was used for regression runs; the final judgement is always a real PSP.
