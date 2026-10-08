#!/bin/bash
# Builds the unified EBOOT of a release from this tree: the PSP-1000 and the
# PSP-2000/3000/Go runtimes (native/psp_ge), the launcher (native/psp_launcher),
# then packs and audits it (tools/unified). Needs the PSPDEV toolchain in
# /usr/local/pspdev (the Makefiles point there) and python3.
# usage: tools/release/build_release.sh [output folder, default ./dist]
# Run it in the public tree (th10-psp-native): the archive step takes licenses/,
# README_EN.md and THIRD_PARTY.md from the root.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$(mkdir -p "${1:-$ROOT/dist}" && cd "${1:-$ROOT/dist}" && pwd)
VERSION=v1.0.1
# The text table that ships next to the EBOOT (Noto Sans CJK JP, SIL OFL 1.1):
# licenses/NotoSansJP/FONTLOG-TH10PSP.txt.
FONT_TABLE="$ROOT/tools/fonts/th10_font32_noto.bin"
FONT_SHA256=60873ef26ffec905a4995aa9e0d8c568da3dc60db6e90c51b8b1a5cd5a6695b5
BASE='-DTH10_EXTENDED_INLINE=0 -ffunction-sections'
# PSP-2000/3000/Go: 64 MB, GE 4 MiB eDRAM mode, ME audio, SELECT recording.
GO_FLAGS='-DTH10_REPLAY_ALIGN_FIX=1 -DTH10_PACING_NO_CATCHUP=1 -DTH10_DF_LEAN=1 -DTH10_FIXED_VERTEX=1 -DTH10_TRIG_MEMO=1 -DTH10_ANM_SCROLL_SKIP=1 -DTH10_SPRITE_ROT_RANGE=1 -DTH10_WINDOW_BREAKDOWN=1 -DTH10_ANM_VERTEX_CAP=32768 -DTH10_GE_NO_BACKBUFFER_BYTES=1 -DTH10_QUAD_INDEX=1 -DTH10_HOT_MOVE=1 -DTH10_FAST_ANGLE=1 -DTH10_FAST_SHOT=1 -DTH10_FAST_ALIGN=1 -DTH10_FAST_TIMER=1 -DTH10_FAST_BILLBOARD=1 -DTH10_FAST_PACK=1 -DTH10_FAST_FLOOR=1 -DTH10_FAST_FEATURES=1 -DTH10_FAST_OUTSIDE=1 -DTH10_FAST_EASING=1 -DTH10_GUARD_CLIP=1 -DTH10_FAST_RING=1 -DTH10_FAST_LASER=1 -DTH10_MISS_BREAKDOWN=1 -DTH10_TRANSITION_DRAW=1 -DTH10_FAST_EASING_INT=1 -DTH10_FAST_EASING_INLINE=1 -DTH10_FAST_ANIMATE=1 -DTH10_FAST_ANGLE_FLOAT=1 -DTH10_FAST_TEXT=1 -DTH10_FAST_STAGE_CULL=1 -DTH10_FAST_MODEL=1 -DTH10_TRANSFORM_MEMO=1 -DTH10_GUARD_NEAR=1 -DTH10_FAST_TEXT_UPLOAD=1 -DTH10_BULLET_GATE=1 -DTH10_FAST_TURN=1 -DTH10_FAST_ITEM=1 -DTH10_ITEM_POLAR_MEMO=1 -DTH10_FAST_BIND=1 -DTH10_TIMER_INLINE=1 -DTH10_FAST_CANCEL=1 -DTH10_FAST_PLAYFIELD=1 -DTH10_REC=1 -DTH10_BGM_PREFETCH=1 -DTH10_RESULT_KEEP_PREV=1 -DTH10_REPLAY_STAGE_LEAD=1 -DTH10_REPLAY_FAITH_CURSOR=1 -DTH10_FAST_RNG_UNIT=1 -DTH10_RENDER_FAST_BULLETS=1 -DTH10_RENDER_FAST_GE_STATE=1 -DTH10_WORLD_BAKE=1'
# PSP-1000: 32 MB, 16-bit and CLUT8 textures, sound effects in ME eDRAM.
PSP1000_FLAGS='-DTH10_REPLAY_ALIGN_FIX=1 -DTH10_PACING_NO_CATCHUP=1 -DTH10_DF_LEAN=1 -DTH10_FIXED_VERTEX=1 -DTH10_TRIG_MEMO=1 -DTH10_ANM_SCROLL_SKIP=1 -DTH10_SPRITE_ROT_RANGE=1 -DTH10_WINDOW_BREAKDOWN=1 -DTH10_ANM_VERTEX_CAP=16384 -DTH10_GE_NO_BACKBUFFER_BYTES=1 -DTH10_QUAD_INDEX=1 -DTH10_HOT_MOVE=1 -DTH10_FAST_ANGLE=1 -DTH10_FAST_SHOT=1 -DTH10_FAST_ALIGN=1 -DTH10_FAST_TIMER=1 -DTH10_FAST_BILLBOARD=1 -DTH10_FAST_PACK=1 -DTH10_FAST_FLOOR=1 -DTH10_FAST_FEATURES=1 -DTH10_FAST_OUTSIDE=1 -DTH10_FAST_EASING=1 -DTH10_GUARD_CLIP=1 -DTH10_FAST_RING=1 -DTH10_FAST_LASER=1 -DTH10_MISS_BREAKDOWN=1 -DTH10_TRANSITION_DRAW=1 -DTH10_FAST_EASING_INT=1 -DTH10_FAST_EASING_INLINE=1 -DTH10_FAST_ANIMATE=1 -DTH10_FAST_ANGLE_FLOAT=1 -DTH10_FAST_TEXT=1 -DTH10_FAST_STAGE_CULL=1 -DTH10_FAST_MODEL=1 -DTH10_TRANSFORM_MEMO=1 -DTH10_GUARD_NEAR=1 -DTH10_FAST_TEXT_UPLOAD=1 -DTH10_BULLET_GATE=1 -DTH10_FAST_TURN=1 -DTH10_FAST_ITEM=1 -DTH10_ITEM_POLAR_MEMO=1 -DTH10_FAST_BIND=1 -DTH10_TIMER_INLINE=1 -DTH10_FAST_CANCEL=1 -DTH10_FAST_PLAYFIELD=1 -DTH10_RESULT_KEEP_PREV=1 -DTH10_REPLAY_STAGE_LEAD=1 -DTH10_REPLAY_FAITH_CURSOR=1 -DTH10_TEXTURE_16BIT=2 -DTH10_VOLATILE_ARENA=1 -DTH10_GE4=0 -DTH10_MAIN_STACK_KB=256 -DTH10_ME_ALLOW_FAT=1 -DTH10_ANM_POOL_CAP=2048 -DTH10_SE_EDRAM=1 -DTH10_ANM_STREAM_LOAD=1 -DTH10_TEXTURE_CLUT8=1 -DTH10_TEXTURE_LAZY_EMPTY=1 -DTH10_HEAP_HWM=1 -DTH10_SCORE_SAVE_EXACT=1 -DTH10_LIST_KB=384 -DTH10_SCORE_LOAD_RELEASE=1 -DTH10_STAT_CAP=9000 -DTH10_TRACE_FLUSH_KB=64 -DTH10_TEXTURE_LAZY_PADDED=1 -DTH10_FAST_RNG_UNIT=1 -DTH10_RENDER_FAST_BULLETS=1 -DTH10_RENDER_FAST_GE_STATE=1 -DTH10_WORLD_BAKE=1'

runtime() {  # objdir build_id title flags ldscript output
  make -C "$ROOT/native/psp_ge" -j"$(nproc)" OBJDIR="$1" LDSCRIPT="$5" TH10_BUILD_ID="$2" \
    "EXTRA_CXXFLAGS=$BASE $4" "PSP_EBOOT_TITLE=$3"
  grep -a -q "TH10_BUILD_ID=$2" "$ROOT/native/psp_ge/EBOOT.PBP"
  cp "$ROOT/native/psp_ge/EBOOT.PBP" "$6"
}
runtime obj_release_go th10-$VERSION-go 'TH10 PSP (GE)' "$GO_FLAGS" psp_hot2p.ld "$OUT/runtime_go.pbp"
runtime obj_release_1000 th10-$VERSION-1000 'TH10 PSP 1000' "$PSP1000_FLAGS" psp_hot2p.ld "$OUT/runtime_1000.pbp"
make -C "$ROOT/native/psp_launcher" clean all
python3 "$ROOT/tools/unified/pack_unified_pbp.py" \
  --launcher "$ROOT/native/psp_launcher/EBOOT.PBP" \
  --psp1000 "$OUT/runtime_1000.pbp" --psp2000plus "$OUT/runtime_go.pbp" \
  --ge4wrap "$ROOT/native/psp/ge4wrap_texv1.prx" --output "$OUT/EBOOT.PBP"
python3 "$ROOT/tools/unified/audit_unified_pbp.py" "$OUT/EBOOT.PBP" \
  --psp1000-sha256 "$(sha256sum "$OUT/runtime_1000.pbp" | cut -c1-64)" \
  --psp2000plus-sha256 "$(sha256sum "$OUT/runtime_go.pbp" | cut -c1-64)"
echo "$FONT_SHA256  $FONT_TABLE" | sha256sum -c --quiet
python3 "$ROOT/tools/unified/check_no_original_assets.py" "$OUT/EBOOT.PBP" "$FONT_TABLE"
chmod 644 "$OUT/EBOOT.PBP"
sha256sum "$OUT/EBOOT.PBP"
# The release archive: the game folder with the EBOOT, the text table, the
# documents and every license text the binaries and the table need.
python3 - "$ROOT" "$OUT" "$VERSION" <<'PY'
import sys, zipfile
from pathlib import Path
root, out, version = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
files = {
    "EBOOT.PBP": out / "EBOOT.PBP",
    "th10_font32.bin": root / "tools/fonts/th10_font32_noto.bin",
    "README.md": root / "README.md",
    "README_EN.md": root / "README_EN.md",
    "LICENSE": root / "LICENSE",
    "THIRD_PARTY.md": root / "THIRD_PARTY.md",
    "licenses/atracdenc-LGPL-2.1.txt": root / "native/third_party/atracdenc/LICENSE",
    "licenses/me-custom-core-MIT.md": root / "native/psp/third_party/me-custom-core/LICENSE.md",
    "licenses/th10-rebuilt.LICENSE": root / "th10_web/assets/vendor/th10-rebuilt.LICENSE",
    "licenses/NotoSansJP/OFL.txt": root / "licenses/NotoSansJP/OFL.txt",
    "licenses/NotoSansJP/FONTLOG-TH10PSP.txt": root / "licenses/NotoSansJP/FONTLOG-TH10PSP.txt",
}
for lic in sorted((root / "licenses").glob("*.txt")):
    files["licenses/" + lic.name] = lic
archive = out / f"th10-psp-native-{version}.zip"
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
    for name, source in files.items():
        z.write(source, "TH10PSP/" + name)
print(f"{archive} ({len(files)} files)")
PY
