#!/bin/sh
# Builds selfwrap_check (the launcher's xmb_selfwrap.cpp on the PC).
# usage: build.sh <libpng 1.6 source folder> <output folder>
set -eu
PNG=$1; OUT=$2; HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT/png"
cp "$PNG/scripts/pnglibconf.h.prebuilt" "$OUT/png/pnglibconf.h"
for c in png pngerror pngget pngmem pngpread pngread pngrio pngrtran pngrutil pngset pngtrans pngwio pngwrite pngwtran pngwutil; do
  [ "$OUT/png/$c.o" -nt "$PNG/$c.c" ] || gcc -O2 -c -I"$OUT/png" -I"$PNG" "$PNG/$c.c" -o "$OUT/png/$c.o"
done
# The ATRAC3 encoder subset (atracdenc.mk lists the same files).
A="$HERE/../../third_party/atracdenc/src"
AINC="-I$A -I$A/lib -I$A/lib/fft/kissfft_impl -I$A/lib/fft/kissfft_impl/tools"
mkdir -p "$OUT/atrac"
for f in atrac3denc atrac/at3/atrac3 atrac/at3/atrac3_bitstream atrac/atrac_scale atrac/atrac_enc_cache \
         atrac/atrac_psy_common atrac/at1/atrac1 atrac/at3p/at3p_tables lib/mdct/mdct lib/bitstream/bitstream \
         lib/bs_encode/encode qmf/qmf transient_detector transient_spectral_upsampler env; do
  o="$OUT/atrac/$(echo $f | tr / _).o"
  [ "$o" -nt "$A/$f.cpp" ] || g++ -std=gnu++17 -O2 -w $AINC -c "$A/$f.cpp" -o "$o"
done
for f in lib/fft/kissfft_impl/kiss_fft lib/fft/kissfft_impl/tools/kiss_fftr; do
  o="$OUT/atrac/$(echo $f | tr / _).o"
  [ "$o" -nt "$A/$f.c" ] || gcc -O2 -w $AINC -c "$A/$f.c" -o "$o"
done
g++ -std=gnu++17 -O2 -Wall -Wextra -Werror -I"$HERE" -I"$HERE/.." -I"$OUT/png" -I"$PNG" $AINC \
  "$HERE/../xmb_selfwrap.cpp" "$HERE/../xmb_sound.cpp" "$HERE/psp_io_host.cpp" "$HERE/selfwrap_check.cpp" \
  "$OUT"/png/*.o "$OUT"/atrac/*.o -lz -o "$OUT/selfwrap_check"
echo "built $OUT/selfwrap_check"
