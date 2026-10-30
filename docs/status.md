# Status

## Builds (CI, every push to main)

| Target | State | Where to get it |
|---|---|---|
| Switch `kk4s.nro` (+ `.elf`) | builds | [nightly release](https://github.com/navik677/kk4s/releases/tag/nightly) |
| PC viewer, Linux (ASan + UBSan) | builds, smoke test passes (hello game runs 15 s under xvfb without crashing); FSN runs locally on Fedora | Actions → desktop → artifact `kk4s-linux-<sha>` |
| PC viewer, Windows (MinGW) | builds, runs FSN to the intro locally | Actions → desktop → artifact `kk4s-windows-<sha>` (exe + DLLs) |

Nothing has been run on a real Switch yet. The Windows PC viewer runs locally (built with MSYS2).

## Real game: Fate/stay night (Kirikiroid2-ready folder)

Plays on the PC viewer (Windows), unmodified — intro, prologue, Russian text, menus: encrypted archives are decrypted through the folder's `xp3filter.tjs`, `Override2.tjs` / `AfterInit2.tjs` run, util.dll / p.dll are replaced natively. See [kirikiroid-compat.md](kirikiroid-compat.md).

## M0 — first build: done

- Launcher: Switch text menu over `sdmc:/switch/kk4s/games/`; PC: `kk4s <game folder>`, or a dialog/terminal list over `games/` next to the exe. Last played game is remembered.
- Game folder acts as the exe folder (`System.exePath`), so `data.xp3` / `patch*.xp3` behave like on Windows.
- Project dir: `startup.tjs` → `data.xp3>` → `data/` → folder (case-insensitive).
- One log file per run in `<data folder>/logs/`, fsync after every line, build id + memory/applet mode at start; engine console log goes there too. nxlink on Switch.
- SDL renderer falls back to software when there is no GPU; window/renderer errors are logged.

Engine fixes so far (all marked `kk4s:` in `engine/`):
- **Relative storage names never used the current directory** (krkrs prefixed every name with `file:/`), so nothing inside `data.xp3>` could be found. Now only native absolute paths get the prefix.
- Engine log (`TVPAddLog`: script errors, `Debug.message`) goes to the kk4s log file (UTF-8).
- TJS register stack was shared by all threads: a TJS xp3 filter running on the audio thread corrupted the main script (random "member does not exist"). Non-main threads now get their own stack.
- Fonts: game-folder fonts and OS fonts are registered; unknown font names fall back to the default font instead of failing; system fonts are found even when the game adds its own.
- Fonts with full-width Cyrillic and proportional Latin (Japanese fonts: MS Gothic, FSN's "Mona") take Cyrillic from a proportional system font (Droid Sans, Meiryo, Segoe UI, Nintendo Standard, ...), aligned to the main font's baseline. This fixed the wide letter spacing in FSN's Russian text.
- Linux: storage names are lower-cased by the engine; on case-sensitive file systems the real spelling is found (`Override2.tjs`, `AfterInit2.tjs` were not run before). A relative game folder on the command line now works.
- `Plugins.link` of an already linked internal plugin no longer counts as missing.
- FFmpeg audio decoder ported to the send/receive API (old API was removed in FFmpeg 5).
- System headers were included inside `namespace TJS` (broke with current newlib).
- TLG5 compressor wrote one byte past its buffer.
- Missing `return` in `sdlProcessEventsForFrames`.
- MinGW: `_WIN32` checks routed to the SDL/POSIX path, `d_type`/`realpath`/`mkdir` shims.

## Not done / not checked

- Plugins: see [plugins.md](plugins.md) (several are approximations or stubs).
- Sound not checked by ear yet; video not tested.
- Non-ASCII game folder names (narrow → wide path conversion).
- No large-stack thread for TJS, no memory budget yet.
- Saves are written where the game asks (inside the game folder) — redirect comes in M2.
- Windows build: 16-bit `wchar_t` differs from Switch/Linux (works so far, keep an eye on text handling).
