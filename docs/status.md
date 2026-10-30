# Status

## Builds (CI, every push to main)

| Target | State | Where to get it |
|---|---|---|
| Switch `kk4s.nro` (+ `.elf`) | builds | [nightly release](https://github.com/navik677/kk4s/releases/tag/nightly) |
| PC viewer, Linux (ASan + UBSan) | builds, smoke test passes (hello game runs 15 s under xvfb without crashing) | Actions → desktop → artifact `kk4s-linux-<sha>` |
| PC viewer, Windows (MinGW) | builds (experimental) | Actions → desktop → artifact `kk4s-windows-<sha>` (exe + DLLs) |

Nothing has been run on a real Switch or a real Windows PC yet.

## M0 — first build: done

- Launcher: Switch text menu over `sdmc:/switch/kk4s/games/`; PC: `kk4s <game folder>`, or a dialog/terminal list over `games/` next to the exe. Last played game is remembered.
- Game folder acts as the exe folder (`System.exePath`), so `data.xp3` / `patch*.xp3` behave like on Windows.
- Project dir: `startup.tjs` → `data.xp3>` → `data/` → folder (case-insensitive).
- One log file per run in `<data folder>/logs/`, fsync after every line, build id + memory/applet mode at start; engine console log goes there too. nxlink on Switch.
- SDL renderer falls back to software when there is no GPU; window/renderer errors are logged.

Engine fixes so far (all marked `kk4s:` in `engine/`):
- FFmpeg audio decoder ported to the send/receive API (old API was removed in FFmpeg 5).
- System headers were included inside `namespace TJS` (broke with current newlib).
- TLG5 compressor wrote one byte past its buffer.
- Missing `return` in `sdlProcessEventsForFrames`.
- MinGW: `_WIN32` checks routed to the SDL/POSIX path, `d_type`/`realpath`/`mkdir` shims.

## Not done / not checked

- Real game compatibility. A Kirikiroid2-ready test game (encrypted xp3 + many plugins) is expected to stop early: needs `xp3filter.tjs` support and plugins, see [kirikiroid-compat.md](kirikiroid-compat.md) and [roadmap.md](roadmap.md).
- Non-ASCII game folder names (narrow → wide path conversion).
- No large-stack thread for TJS, no memory budget yet.
- Saves are written where the game asks (inside the game folder) — redirect comes in M2.
- Windows build: 16-bit `wchar_t` differs from Switch/Linux; not run yet.
