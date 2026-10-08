#!/bin/sh
# Builds text_table_check (the launcher's text_table.cpp with the bundled
# FreeType, on the PC). Compare its table with make_font_msgothic.py's:
#   text_table_check <msgothic.ttc> <table from make_font_msgothic.py>
# usage: build_text_table_check.sh <output folder>
set -eu
OUT=$1; HERE=$(cd "$(dirname "$0")" && pwd)
FT="$HERE/../../third_party/freetype"
FTINC="-I$FT/th10 -I$FT/include"
mkdir -p "$OUT/freetype"
for f in base/ftbase base/ftinit base/ftsystem base/ftglyph base/ftbitmap base/ftdebug base/ftmm \
         truetype/truetype sfnt/sfnt smooth/smooth; do
  o="$OUT/freetype/$(echo $f | tr / _).o"
  [ "$o" -nt "$FT/src/$f.c" ] || gcc -O2 -w -DFT2_BUILD_LIBRARY $FTINC -c "$FT/src/$f.c" -o "$o"
done
g++ -std=gnu++17 -O2 -Wall -Wextra -Werror -I"$HERE/.." $FTINC \
  "$HERE/../text_table.cpp" "$HERE/text_table_check.cpp" "$OUT"/freetype/*.o -o "$OUT/text_table_check"
echo "built $OUT/text_table_check"
