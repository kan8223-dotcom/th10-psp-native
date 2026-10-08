# ge4wrap_texv1 (GE 4 MiB eDRAM mode helper)

The kernel PRX `../ge4wrap_texv1.prx` that the PSP-2000/3000/Go runtime loads
(`../Ge4.cpp`) to switch the GE's eDRAM to 4 MiB. It is the TH07 PSP port's
wrapper, reused unchanged.

Build it from this folder with the PSPDEV toolchain (`psp-gcc`, `psp-config`,
`psp-prxgen`, `psp-build-exports`, `psp-fixup-imports` on the PATH):

    make
    python3 build_ge4_slimplus_wrapper.py --input bin/ge4wrap_texv1.prx --output ../ge4wrap_texv1.prx

- `make` builds the base wrapper `bin/ge4wrap_texv1.prx` (2,150 bytes,
  SHA-256 `411e71b3ffb31bd91024cc0221481a787e693276c0899e05da08c3cd91dc1ab8`
  with pspsdk 800ce356). It accepts model 3 (PSP-3000) only.
- `build_ge4_slimplus_wrapper.py` checks that hash, replaces the two
  instructions of the model check at offset 0x84 so that every model from 1
  (PSP-2000) up is accepted and model 0 (PSP-1000) and errors are still
  refused, and checks the result:
  SHA-256 `3dc5c753497349d6fb0ab5ae2a819b240cc51e8aa412ded10bb52daa540d841d`.
  That file is the shipped `../ge4wrap_texv1.prx`; the launcher, the packer and
  the runtime Makefile pin this hash.

Exports (library `ge4wrap_texv1`): `ge4TexV1GetEdramHwSize` 0x2DDAC688,
`ge4TexV1GetModel` 0xBB75238F, `ge4TexV1SetEdramSize` 0x703B997B.
