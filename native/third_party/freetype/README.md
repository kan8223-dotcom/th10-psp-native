# FreeType (subset)

The unified launcher (`native/psp_launcher/text_table.cpp`) uses this code to
make the text table `th10_font32.bin` on the PSP from the user's own MS Gothic
(`msgothic.ttc`).

- Upstream: https://freetype.org/
- Version: 2.14.1, from `freetype-2.14.1.tar.xz`
  (https://download.savannah.gnu.org/releases/freetype/freetype-2.14.1.tar.xz,
  2,664,948 bytes, SHA-256
  `32427e8c471ac095853212a37aef816c60b42052d4d9e48230bab3bdf2936ccc`).
  This is the FreeType version inside Pillow 12.1.1, which
  `tools/fonts/make_font_msgothic.py` uses on the PC; with it the launcher
  writes the same bytes as that script.
- License: FreeType offers the FreeType License (FTL) or the GPL v2; this port
  uses it under the FTL (`docs/FTL.TXT`, `LICENSE.TXT`). As the FTL asks:
  Portions of this software are copyright © 2025 The FreeType Project
  (https://freetype.org). All rights reserved.

## What is here

`include/` unchanged, and from `src/` only what the launcher builds
(`psp_launcher/freetype.mk` lists the compiled files): the base layer
(`ftbase.c` and the files it includes, `ftinit.c`, `ftsystem.c`, `ftglyph.c`,
`ftbitmap.c`, `ftdebug.c`, `ftmm.c`, `md5.c`), the TrueType driver
(`src/truetype`), the SFNT tables (`src/sfnt`) and the anti-aliased renderer
(`src/smooth`). These files are unmodified.

## Local configuration

`th10/` comes before `include/` on the include path, so its two headers take
the place of FreeType's own:

- `th10/freetype/config/ftmodule.h`: only the TrueType driver, the SFNT module
  and the smooth renderer are registered.
- `th10/freetype/config/ftoption.h`: FreeType 2.14.1's `ftoption.h` with
  `FT_CONFIG_OPTION_USE_LZW` and `FT_CONFIG_OPTION_USE_ZLIB` turned off,
  because the LZW and gzip modules are not built. Hinting, the bytecode
  interpreter and every other option keep FreeType's defaults.
