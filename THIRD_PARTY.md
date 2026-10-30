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

Fonts: none bundled yet. M6 will add a free CJK font (Noto Sans CJK, SIL OFL 1.1).
