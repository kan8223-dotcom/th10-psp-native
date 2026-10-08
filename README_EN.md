[日本語](README.md) | **English**

# Touhou 10 for PSP (th10-psp-native)

An unofficial port of 東方風神録 ～ Mountain of Faith. (Touhou 10) to the PSP. It is not an emulator: the upstream C++ reimplementation ([YomotsuHisami/th10](https://github.com/YomotsuHisami/th10)) runs on the PSP's CPU and GE.

One EBOOT covers the PSP-1000 and the PSP-2000 / 3000 / Go: at start it checks the model and launches the matching build.

> This project is not affiliated with GensokyoClub and does not use its decompilations. Please do not contact the original author, the upstream projects or anyone credited here about this port.

## Requirements

- A PSP (1000 / 2000 / 3000 / Go) with ARK-5. Tested on a PSP Go and a PSP-1000, both with ARK-5.
- On a PSP-2000 / 3000 / Go, set this game's "Use Extra Memory" to "Max" in the ARK-5 settings. With "Default" or "Off" it may fail to start. (This does not apply to the PSP-1000.)
- Your own copy of Touhou 10 **version 1.00a**:
  - `th10.dat` (27,696,219 bytes)
  - `thbgm.dat` (403,789,620 bytes)

Data files with other sizes (other versions) are refused.

## Install

1. Unpack the release ZIP and copy its `TH10PSP` folder to `PSP/GAME/` on the Memory Stick.
2. Put `th10.dat` and `thbgm.dat` in that folder (`PSP/GAME/TH10PSP/`).
3. Under Game on the XMB, an item with no icon and no title appears: start it. Back on the XMB afterwards, the item has its icon, background and music.

The icon, background and music (the first 27 seconds of the title screen theme) are made from your game data on the first start only and written into the EBOOT. This takes about 30 seconds and shows a progress bar; do not turn the PSP off meanwhile.

The release contains no image or sound made from the original game (only transparent images and silence). After that first start the EBOOT holds material made from your data: do not redistribute it.

Saves and replays go to `save/` in the same folder.

## Controls

| PSP | Game |
|---|---|
| D-pad, analog stick | move |
| Cross | shot, OK |
| Circle | bomb, cancel |
| Square / L / R | focus (slow) |
| Triangle | skip dialogue |
| START | pause |
| SELECT | start/stop recording (PSP-2000/3000/Go only; AVI files in `ms0:/VIDEO/TH10/`) |

## Text table (th10_font32.bin)

The game draws its text from `th10_font32.bin` in the EBOOT's folder. The release includes a table made from Noto Sans JP (SIL Open Font License 1.1); it works as it is.

For the original game's MS Gothic look, copy `C:\Windows\Fonts\msgothic.ttc` from your Windows into the folder with `th10.dat`, then start the game. It makes a table from MS Gothic and puts it in place of `th10_font32.bin` (about 2 seconds on a PSP-1000, with a progress bar). The bundled Noto table stays as `th10_font32_noto.bin`.

- If a newer release puts the Noto table back as `th10_font32.bin`, the next start makes the MS Gothic table again, as long as `msgothic.ttc` is still there.
- To go back to Noto, delete `msgothic.ttc` and `th10_font32.bin`, and rename `th10_font32_noto.bin` to `th10_font32.bin`.
- A file named `msgothic.ttc` that is not MS Gothic shows the reason for 4 seconds at each start, and the current table stays in use.
- `msgothic.ttc` and a table made from it are for your own use only: never redistribute them.
- A PC can make it too ([tools/fonts/README.md](tools/fonts/README.md); needs Python); with Pillow 12.1.1 it is the same table the PSP makes.

## Numbers measured on hardware

Game logic runs at 60 ticks per second and drawing at 30 fps (one frame per two ticks). "Late frames" is the share of 33.3 ms frames whose two ticks and drawing did not fit.

| Item | PSP Go | PSP-1000 | Conditions |
|---|---|---|---|
| Late frames | 14.51% | 14.06% | Go: replay playback, stages 4-6; 1000: own play, stage 4 (2026-10-07) |
| Free memory at start | 48.4 MB | 21.2 MB | right after the game build starts |
| Peak memory used | 34.2 MB | 17.6 MB | Go: stages 4-6; 1000: stage 6 |
| First-start XMB generation | 29.1 s | 26.7 s | 2026-10-08 |
| Actual CPU clock | 418 MHz | 459 MHz | set by ARK-5 (the game asks for 333 MHz) |

How these were measured, and what was tried without success, is in [docs/](docs/) (Japanese).

## Building from source

Needs the PSPDEV toolchain (in `/usr/local/pspdev`) and python3.

    tools/release/build_release.sh

This writes `dist/EBOOT.PBP`: the two game builds (PSP-1000 and PSP-2000/3000/Go), the launcher that picks one at start, and the GE4 helper PRX. The script also checks the result for original game data.

`native/CMakeLists.txt` builds the PC check program, which writes the game state to a trace.

## About development

The code was written by AI; my part was directing it and several days of intense debugging. Most of the achievement belongs to the upstream reimplementation (YomotsuHisami/th10).

## Thanks

- Team Shanghai Alice / ZUN — the original game
- YomotsuHisami — the Touhou 10 reimplementation ([YomotsuHisami/th10](https://github.com/YomotsuHisami/th10))
- Saekaze — the Touhou 10 Switch port (th10-switch), which started and informed this port
- M-cid (m-c/d) — PSP Media Engine Custom Core
- Daniil Cherednik — atracdenc (encodes the XMB music)
- The FreeType Project — FreeType (makes the MS Gothic text table on the PSP)
- Adobe / Google — Noto Sans CJK JP (the source of the bundled text table)

## License

The code written for this port is under the MIT License ([LICENSE](LICENSE)). The upstream reimplementation and the bundled components keep their own terms ([THIRD_PARTY.md](THIRD_PARTY.md)). The text table `th10_font32.bin` is a Modified Version of Noto Sans JP under the SIL Open Font License 1.1, not the MIT License ([licenses/NotoSansJP/](licenses/NotoSansJP/)). The original game is © Team Shanghai Alice.
