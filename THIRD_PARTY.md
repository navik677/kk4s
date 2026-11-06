# Third-party code and licenses

| Component | Where | License |
|---|---|---|
| Kirikiri 2 / Kirikiri Z core (W.Dee and contributors) | `engine/source/` | Modified BSD (Japanese), see `engine/LICENSE` |
| krkrs Switch port (uyjulian) | `engine/source/` | same as Kirikiri, see `engine/LICENSE` |
| Libraries bundled in krkrs sources (ncbind, tp_stub, md5, cp932 tables, TLG, etc.) | `engine/source/` | see `engine/LICENSE.3rdparty` |
| libnx | linked (devkitPro) | ISC |
| SDL2 (switch port) | linked (switch-portlibs) | zlib |
| FFmpeg (libavcodec/format/filter/util, swscale) | linked (switch-portlibs) | LGPL-2.1+ (check the portlibs build config: if built with GPL parts, the .nro is GPL) |
| FreeType | linked | FreeType License (FTL) |
| libpng | linked | libpng license |
| libjpeg-turbo | linked | IJG / BSD-3 / zlib |
| libwebp | linked | BSD-3 |
| libogg, libvorbis | linked | BSD-3 |
| opus, opusfile | linked | BSD-3 |
| zlib | linked | zlib |
| Oniguruma | linked (built from source if not in portlibs) | BSD-2 |
| AlphaMovie plugin (T.Imoto, wamsoft/AlphaMovie) | `engine/source/plugins/AlphaMovie.cpp` | same as Kirikiri, see `engine/LICENSE` |
| XP3 crypt family definitions (morkt, GARbro `ArcFormats/KiriKiri/CryptAlgorithms.cs`) | reimplemented in `compat/kk4s_xp3crypt.cpp` (keyless families only, no game keys) | MIT |
| KAG3 template system scripts (W.Dee), test fixture only | `tests/kag3/template/` | Kirikiri license or GPL-2 (dual), see `tests/kag3/template/README.md` |
| layerExBtoA / layerExImage / layerExRaster contracts (Go Watanabe, wamsoft) | reimplemented in `compat/kk4s_layerex.cpp` | same as Kirikiri, see `engine/LICENSE`; layerExImage's colour and blur maths come from CxImage (Davide Pizzolato, zlib-style license) |

Fonts: none bundled yet. M6 will add a free CJK font (Noto Sans CJK, SIL OFL 1.1).

## extrans transitions (`engine/source/plugins/extrans/`)

Ported from krkrz's extrans plugin (wave, mosaic, turn, rotate*, ripple),
Copyright (C) 2000-2009 W.Dee <dee@kikyou.info> and contributors, under the
Kirikiri license like the engine itself. kk4s changes are marked `kk4s:`; the
422 KB generated `turntrans_table.cpp` is replaced by the generator's
arithmetic (`maketable/mkturntranstable.pl`), verified to give the same table.

## KAGParserEx (`engine/source/plugins/KAGParserEx/`)

The extended KAG parser of the KAGParserEx plugin (miahmie, wtnbgo; krkrz
`KAGParserEx`, based on Kirikiri's KAGParser by W.Dee), Kirikiri license.
`KAGParserEx_parser.h` / `.inc` are its `KAGParser.h` / `KAGParser.cpp`
(line endings normalised; the constructor initialises the position state,
marked `kk4s:`).

## saveStruct (`engine/source/plugins/saveStruct.cpp`)

wamsoft/saveStruct and its json `Writer.hpp` (W.Dee / miahmie / GoWatanabe),
Kirikiri license, adapted to krkrs' streams.

## psdfile / psdparse (`engine/source/plugins/psdfile/`)

- psdfile glue (`main.cpp`, `psdclass*`): wamsoft/psdfile by GoWatanabe,
  Kirikiri license; kk4s changes marked `kk4s:` (RGBA layer order, krkrs
  streams, packed BMP header, no license-registry call).
- `psdparse/`: wamsoft/psdparse, MIT License, Copyright (c) 2026 wamsoft
  (`psdparse/LICENSE`, upstream commit in `psdparse/UPSTREAM.txt`).

## layerExDraw (`engine/source/plugins/layerExDraw/`)

- `main.cpp`: wamsoft/layerExDraw's ncbind glue (Kirikiri license), only GCC
  template fixes. `LayerExDraw.cpp/.hpp`, `gdip_kk4s.h`: kk4s' own GDI+
  stand-in over plutovg.
- `plutovg/`: sammycage/plutovg, MIT License (`plutovg/LICENSE`); the
  `plutovg-ft-*` files are FreeType code under the FreeType License
  (`plutovg/FTL.TXT`); stb_truetype / stb_image (public domain / MIT) are
  bundled in it. Upstream commit in `plutovg/UPSTREAM.txt`; kk4s change:
  a file-read fallback instead of `mmap` on the Switch (marked `kk4s:`).

## Other engines (`engines/`), separate programs

kk4s starts these as their own programs (docs/engines.md); they are not linked
into kk4s.nro, so their licenses cover only their own binaries.

- `engines/onscripter-nx/`: ONScripter-NX by clamintus (Switch port of
  onscripter-20060724-insani-sdl2; ONScripter by Ogapee), GNU GPL v2 or later,
  see `engines/onscripter-nx/COPYING`. kk4s changes are marked `kk4s:`
  (`--kk4s-return`, `--root` enters the game folder, start-up file check).
  Its `romfs/default.ttf` comes with the upstream repository.
- `engines/renpy/renpy.py`: `renpy.py` of Ren'Py 7.3.5 (Tom Rothamel), MIT
  license (header of the file), as shipped with the Ren'Py Switch SDK; kk4s
  lines marked `kk4s:`. The SDK binary (renpy-switch, uyjulian) and its
  `lib.zip` are not in this repository.
