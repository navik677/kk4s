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
