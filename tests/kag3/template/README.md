# KAG3 template (vendored test fixture)

These files are the KAG3 template of Kirikiri 2, copied **unmodified** (Shift_JIS text,
CRLF line ends; `.gitattributes` keeps the bytes) from the Kirikiri 2 source history at
<https://github.com/krkrz/krkr2> (`kirikiri2/trunk/kag3/template/`, commit
`dec49af97e174d31059c3ccd7efc700ba3c6b788`, KAG 3.29-dev.20071022):

- `system/` — the KAG3 system scripts and their glyph images;
- `startup.tjs` — the template's startup script.

They are used by `tests/kag3/smoke.py`, which builds a synthetic game around them
(its own scenario and generated images/sounds) and plays it on the kk4s desktop build.
Almost every KAG3 game ships these scripts, often lightly modified, so they exercise the
engine the way that whole class of games does. No game content is included.

License: Kirikiri 2 and KAG are dual-licensed by W.Dee — the Kirikiri license
(`LICENSE-kirikiri2.txt`, UTF-8 copy of the original `license.txt`) or the GNU GPL v2
(`GPL-2.txt`). The kk4s engine sources carry the same Kirikiri license (`engine/LICENSE`).
