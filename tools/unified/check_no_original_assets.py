#!/usr/bin/env python3
"""Reject original TH10 data, personal data and locally generated XMB media.

The distributable unified candidate has exact tool-generated neutral
placeholders in its fixed ICON0/PIC1/SND0 slots (two transparent PNGs and about
a second of silent ATRAC3) and empty ICON1/PIC0 slots. After the owner supplies
valid original data, the PSP overwrites the three fixed slots in that *local*
copy. Such a self-wrapped copy must never be committed or redistributed, nor
may the original th10.dat/thbgm.dat, a text table made from a system font
(MS Gothic), replays or saves. The one text table that may ship is the release's
Noto Sans CJK JP table (SIL OFL 1.1), accepted by its SHA-256 only.

The size checks are deliberately coarse: any file of exactly th10.dat's or
thbgm.dat's size is rejected, whatever its name or content.

Typical pre-commit use::

    git diff --cached --name-only -z --diff-filter=ACMR \
      | xargs -0 python3 tools/check_no_original_assets.py
"""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import struct
import sys
from typing import Iterable

try:
    from . import pack_unified_pbp as packer
except ImportError:  # Direct `python3 tools/check_no_original_assets.py` use.
    import pack_unified_pbp as packer


PBP_HEADER = struct.Struct("<4sI8I")
PBP_MEDIA_PARTS = (1, 2, 3, 4, 5)
PBP_MEDIA_NAMES = {
    1: "ICON0.PNG",
    2: "ICON1.PMF",
    3: "PIC0.PNG",
    4: "PIC1.PNG",
    5: "SND0.AT3",
}

UNIFIED_HEADER = struct.Struct("<8sII")
UNIFIED_ENTRY = struct.Struct("<IIIIII")
UNIFIED_MAGIC = packer.CONTAINER_MAGIC

BAD_NAMES = {
    "icon0.png",
    "icon1.pmf",
    "pic0.png",
    "pic1.png",
    "snd0.at3",
    "th10.dat",
    "th10c.dat",
    "thbgm.dat",
    "thbgm.fmt",
    "th10_font32.bin",   # only the pinned Noto table below; an MS Gothic one is personal
    "th10_font32.tmp",   # the launcher's table being written
    "msgothic.ttc",      # the user's MS Gothic (the launcher makes the table from it)
    "th10runtime.pbp",   # written by the launcher on the PSP
    "th10xmbhelper.pbp",
    "th10unified.log",
}
BAD_SUFFIXES = (".rpy", ".anm", ".std", ".ecl", ".msg", ".sht", ".ttc")

# th10.dat: a 16-byte header encrypted like ResourceArchive (key 0x1b,
# step 0x37, block 16) that decrypts to "THA1".
THA1_MAGIC = 0x31414854
TH10_DAT_SIZE = 27_696_219
THBGM_DAT_SIZE = 403_789_620

# tools/fonts/th10_font32_noto.bin (licenses/NotoSansJP/FONTLOG-TH10PSP.txt).
# Any other "T10F" table, under any name, is rejected.
TEXT_TABLE_MAGIC = b"T10F"
NOTO_TABLE_SHA256 = "60873ef26ffec905a4995aa9e0d8c568da3dc60db6e90c51b8b1a5cd5a6695b5"


def _is_noto_table(path: Path) -> bool:
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest() == NOTO_TABLE_SHA256
    except OSError:
        return False


def _tha1_header(head: bytes) -> bool:
    if len(head) < 16:
        return False
    data = bytearray(16)
    key, sequential = 0x1B, 0
    for lane in range(2):
        for index in range(15 - lane, -1, -2):
            data[index] = head[sequential] ^ key
            key = (key + 0x37) & 0xFF
            sequential += 1
    return struct.unpack_from("<I", data)[0] == THA1_MAGIC


class GuardError(ValueError):
    """A file crosses the repository/distribution asset boundary."""


def _pbp_parts(data: bytes, label: str) -> tuple[bytes, ...]:
    if len(data) < PBP_HEADER.size:
        raise GuardError(f"{label}: truncated PBP")
    magic, _version, *offsets = PBP_HEADER.unpack_from(data)
    if magic != b"\x00PBP":
        raise GuardError(f"{label}: invalid PBP magic")
    if offsets[0] < PBP_HEADER.size or offsets != sorted(offsets):
        raise GuardError(f"{label}: invalid PBP offsets")
    if offsets[-1] > len(data):
        raise GuardError(f"{label}: PBP offset exceeds file size")
    ends = offsets[1:] + [len(data)]
    return tuple(data[start:end] for start, end in zip(offsets, ends))


def _check_pbp(data: bytes, label: str) -> None:
    parts = _pbp_parts(data, label)
    psar = parts[7]
    is_unified = psar.startswith(UNIFIED_MAGIC)
    if is_unified:
        try:
            parsed = packer.parse_pbp(data, label)
            packer._require_neutral_outer_media(parsed)
        except packer.PackError as exc:
            raise GuardError(
                f"{label}: outer XMB media is not the exact neutral placeholder contract"
            ) from exc
    else:
        populated = [PBP_MEDIA_NAMES[index] for index in PBP_MEDIA_PARTS
                     if parts[index]]
        if populated:
            raise GuardError(
                f"{label}: XMB media is populated ({', '.join(populated)})"
            )

    # The unified candidate embeds two complete runtime PBPs and one raw GE4
    # companion in DATA.PSAR. Audit the two nested PBPs for derived media.
    # Audit those payloads too; neutral outer placeholders must not be usable as
    # a wrapper around a derived image in either hidden profile.
    if not psar.startswith(UNIFIED_MAGIC):
        return
    if len(psar) < UNIFIED_HEADER.size:
        raise GuardError(f"{label}: truncated TH10UP02 header")
    magic, version, count = UNIFIED_HEADER.unpack_from(psar)
    if (magic != UNIFIED_MAGIC or version != packer.CONTAINER_VERSION or
            count != 3):
        raise GuardError(f"{label}: unsupported TH10UP02 container")
    table_end = UNIFIED_HEADER.size + count * UNIFIED_ENTRY.size
    if table_end > len(psar):
        raise GuardError(f"{label}: truncated TH10UP02 table")
    expected_profiles = (
        packer.PROFILE_PSP1000,
        packer.PROFILE_PSP2000PLUS,
        packer.COMPANION_GE4,
    )
    expected_offset = table_end
    for index in range(count):
        entry = UNIFIED_ENTRY.unpack_from(
            psar, UNIFIED_HEADER.size + index * UNIFIED_ENTRY.size
        )
        profile, _model_min, _model_max, offset, size, _crc32 = entry
        end = offset + size
        if (profile != expected_profiles[index] or size == 0 or
                offset != expected_offset or end > len(psar)):
            raise GuardError(f"{label}: invalid TH10UP02 payload {index}")
        if profile in (packer.PROFILE_PSP1000, packer.PROFILE_PSP2000PLUS):
            _check_pbp(psar[offset:end], f"{label}/profile-{profile:08x}")
        elif profile != packer.COMPANION_GE4:
            raise GuardError(f"{label}: unknown TH10UP02 member {profile:08x}")
        expected_offset = end
    if expected_offset != len(psar):
        raise GuardError(f"{label}: trailing TH10UP02 data")


def check_path(path: Path) -> str | None:
    if path.name.casefold() in BAD_NAMES and not (
            path.name.casefold() == "th10_font32.bin" and _is_noto_table(path)):
        return f"forbidden original/generated filename: {path.name}"
    if path.suffix.casefold() in BAD_SUFFIXES:
        return f"forbidden original/personal data type: {path.name}"
    try:
        size = path.stat().st_size
        with path.open("rb") as handle:
            head = handle.read(64 * 1024)
            if head[:4] == b"\x00PBP":
                handle.seek(0)
                data = handle.read()
            else:
                data = b""
    except (FileNotFoundError, IsADirectoryError):
        return None
    except OSError as exc:
        return f"cannot inspect file: {exc}"

    if _tha1_header(head):
        return "THA1 archive (th10.dat or a derivative)"
    if head[:4] == TEXT_TABLE_MAGIC and not _is_noto_table(path):
        return "text table that is not the release's Noto one (an MS Gothic table is personal)"
    if head[:4] == b"ZWAV" or size in (TH10_DAT_SIZE, THBGM_DAT_SIZE):
        return "original TH10 data (thbgm.dat/th10.dat size or ZWAV header)"
    if size > 300 * 1024 * 1024:
        return "file exceeds 300 MiB (thbgm.dat suspected)"
    if head[:4] == b"\x00PBP":
        try:
            _check_pbp(data, str(path))
        except GuardError as exc:
            return str(exc)
    if head[:4] == b"RIFF" and head[8:12] == b"WAVE":
        fmt = head.find(b"fmt ")
        if fmt >= 0 and fmt + 10 <= len(head):
            if struct.unpack_from("<H", head, fmt + 8)[0] == 0x0270:
                return "ATRAC3 media (SND0 derivative suspected)"

    # Do not flag source code which necessarily contains these magic strings.
    try:
        head.decode("utf-8")
    except UnicodeDecodeError:
        if b"THTX" in head:
            return "extracted THTX texture"
    return None


def audit(paths: Iterable[str]) -> list[tuple[str, str]]:
    failures: list[tuple[str, str]] = []
    for raw_path in paths:
        reason = check_path(Path(raw_path))
        if reason is not None:
            failures.append((raw_path, reason))
    return failures


def main(argv: list[str] | None = None) -> int:
    failures = audit(sys.argv[1:] if argv is None else argv)
    if not failures:
        return 0
    print(
        "commit/release rejected: original data or locally generated media found",
        file=sys.stderr,
    )
    for path, reason in failures:
        print(f"  {path}: {reason}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
