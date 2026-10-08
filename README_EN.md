[日本語](README.md) | **English**

# Touhou 10 for PSP (th10-psp-native)

An unofficial port of 東方風神録 ～ Mountain of Faith. (Touhou 10) to the PSP. It is not an emulator: the upstream C++ reimplementation ([YomotsuHisami/th10](https://github.com/YomotsuHisami/th10)) runs on the PSP's CPU and GE.

One EBOOT covers the PSP-1000 and the PSP-2000 / 3000 / Go: at start it checks the model and launches the matching build.

> This project is not affiliated with GensokyoClub and does not use its decompilations. Please do not contact the original author, the upstream projects or anyone credited here about this port.

## Requirements

- A PSP (1000 / 2000 / 3000 / Go) with ARK-5. Tested on a PSP Go and a PSP-1000, both with ARK-5.
- Your own copy of Touhou 10 **version 1.00a**:
  - `th10.dat` (27,696,219 bytes)
  - `thbgm.dat` (403,789,620 bytes)
- The text table `th10_font32.bin`, which you make yourself (see "Text table").

Data files with other sizes (other versions) are refused.

## Install

1. Put the release's EBOOT.PBP in a folder under `PSP/GAME/` (for example `PSP/GAME/TH10PSP/`).
2. Put `th10.dat`, `thbgm.dat` and `th10_font32.bin` in the same folder.
3. Start it from the XMB.

On the first start only, it makes the XMB icon, background and music (the first 27 seconds of the title screen theme) from your game data and writes them into the EBOOT. This takes about 30 seconds and shows a progress bar; do not turn the PSP off meanwhile.

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

See [tools/fonts/README.md](tools/fonts/README.md). Two ways to make it:

- **from Noto Sans JP (SIL OFL 1.1)**: may be shared under the OFL;
- **from MS Gothic of your own Windows**: for your own use only; never redistribute it.

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

## License

The code written for this port is under the MIT License ([LICENSE](LICENSE)). The upstream reimplementation and the bundled components keep their own terms ([THIRD_PARTY.md](THIRD_PARTY.md)). The original game is © Team Shanghai Alice.
