# Handoff — read this first in a new session

Last update: 2026-10-31 (end of session). Goal: **any Kirikiri (KAG/TJS2) game folder runs unmodified** on the
Switch (and on PC via the viewer). Game files are never changed; fixes live in the engine,
`compat/` or kk4s' own files. Full requirements: `docs/original-prompt.md`; milestones: `docs/roadmap.md`.

## Where we are

Branch `switch-fixes-2026-10-31` (commit `b4eadd8`, on top of `main`) holds the 2026-10-31 work;
not merged to `main` yet. The Switch build of that state is on the SD card (`kk4s.nro`).

- Works on the Switch: launcher with covers, exit to the launcher (threads stopped, detached
  thread stacks closed), in-game menu on L3 + R3, saves outside the game folder, gamepad + touch.
- Tested games: The Makings of a Lady (plays, but occasional 1–2.5 s stalls after long pages);
  Sharin, Tsukihime (earlier). FSN (Fate/stay night, PC version with `profiles/FSN/xp3filter.tjs`):
  reaches the title, load menu, saves load, the skip-scene prompt (Да/Нет) renders and responds.
- Known broken: choice branch in FSN's flow after a loaded save ("День 7 … Выбор": choice links do
  not appear, scene stops). Video (`krmovie`) is not played, so openings are black.
- `.tpm` encrypted archives (cxdec) are not decrypted: such games need a Kirikiroid
  `xp3filter.tjs`, as in `profiles/<game>/`.

Old Kirikiri 2 (exe without `-evalcontext`) gets two compat switches from `compat/kk4s_compat.cpp`:
`!` on the global object and class `var x;` keeping native properties (`TJSClassVarKeepsProperty`).

## Repo map

| Path | What |
|---|---|
| `engine/` | krkrs (uyjulian). Our edits are marked `kk4s:` (`git grep kk4s: engine/`). Commit `98ac529` is the pristine import. |
| `compat/kk4s_compat.cpp` | Kirikiroid2 conventions (`xp3filter.tjs`, filter API, `Override2.tjs`, `AfterInit2.tjs`), font registration, engine log, save path redirect, volume at start |
| `compat/kk4s_plugins.cpp`, `compat/kk4s_layerfx.cpp` | native replacements for Windows plugin DLLs |
| `platform/common/kk4s_launcher.cpp` | launcher screens; helpers: `kk4s_ui` (drawing, fonts), `kk4s_image` (PNG/JPEG/WebP/ICO/exe icon), `kk4s_i18n` (uk/en/ru/ja), `kk4s_settings` (`settings.ini`), `kk4s_net` (curl + VNDB + JSON), `kk4s_files` (backups) |
| `platform/common/kk4s_overlay.cpp` | in-game menu |
| `platform/common/kk4s_common.cpp` | logger (background writer), game scan, project dir, pad mapping |
| `platform/switch/`, `platform/desktop/` | platform layer: paths, fonts, pad (libnx on Switch), battery, keyboard, exit to launcher |
| `tests/tjs/` | `hello` (smoke test), `input` (shows key/mouse events) |
| `tools/` | `build-switch.sh`, `build-desktop.sh`, `run-linux.sh`, `ci-report.sh`, `resolve-crash.py`, `xp3ls.cpp` |

Engine hook points (all `kk4s:`): `sdl2/stubs.cpp` (main, window, launcher call, input, in-game
menu, exit), `base/win32/SysInitImpl.cpp` (project dir, data path redirect),
`base/android/StorageImpl.cpp` (storage names, case, save redirect), `base/XP3Archive.cpp` (eager
decryption with script filters), `base/ScriptMgnIntf.cpp` (before/after startup scripts),
`base/win32/PluginImpl.cpp` (Plugins.link), `utils/DebugIntf.cpp` (TVPAddLog), `visual/FreeType.cpp`
(font fallback, Cyrillic substitute), `visual/FreeTypeFontRasterizer.cpp` (face cache),
`tjs2/tjsInterCodeExec.cpp` (per-thread register stack).

## Dev setup (Linux, Fedora)

```bash
sudo dnf install SDL2-devel freetype-devel ffmpeg-free-devel libpng-devel libjpeg-turbo-devel \
  turbojpeg-devel libwebp-devel libogg-devel libvorbis-devel opus-devel opusfile-devel zlib-devel \
  oniguruma-devel libcurl-devel
bash tools/build-desktop.sh            # or ASAN=1; DEPS=1 installs deps with apt / pacman only
cmake --build build-desktop -j$(nproc) && cp build-desktop/kk4s out-desktop/   # incremental
KK4S_ROOT=/tmp/kk4s-run ./out-desktop/kk4s game/fsn    # one game; without an argument: launcher
```

Windows (no admin): portable MSYS2 (`msys2-base-x86_64-latest.sfx.exe`), `pacman -Syu` twice, then
`DEPS=1 bash tools/build-desktop.sh` in a UCRT64 shell. Details: `docs/pc-viewer.md`.

Switch builds come from CI (`gh run download <id>`); locally via Docker (`README.md`). On the
Fedora dev box there is only podman, which needs the full image name:
`sed 's#^FROM devkitpro/#FROM docker.io/devkitpro/#' Dockerfile > /tmp/Dockerfile && podman build -t kk4s-build -f /tmp/Dockerfile .`,
then `podman run --rm -v "$PWD":/src:Z kk4s-build bash tools/build-switch.sh` → `out/kk4s.nro`.

## Working with the user's Switch

The user runs an FTP server on the Switch (ftpd, port 5000, anonymous; ask for the IP):

```bash
curl -T out/kk4s.nro ftp://<switch-ip>:5000/switch/kk4s/kk4s.nro        # deploy
curl -s ftp://<switch-ip>:5000/switch/kk4s/logs/                         # list logs
curl -s ftp://<switch-ip>:5000/atmosphere/crash_reports/                 # crash reports
```

A log of a running game cannot be downloaded (the file is open) — ask the user to quit first. FTP
stops when the console sleeps. Big uploads: one file per `curl`, check sizes afterwards.

## Debugging toolbox

- **Log first** (`logs/kk4s-*.log`): script exceptions with TJS stack traces, `compat:` decisions,
  `fonts:`, `input:`, `launcher:`, `overlay:` lines, `slow frame: N ms (compose …, other …)` for frames over 200 ms (fields: `docs/debugging.md`).
- **Crash reports**: Atmosphere report + `kk4s.elf` of the same build (git hash in the log). Without
  devkitPro, `eu-addr2line -f -C -i -e kk4s.elf 0x<offset>` works on the aarch64 ELF; the report
  prints offsets as `kk4s + 0x...`. See `docs/debugging.md`.
- **Screenshots without screen capture** (KDE Wayland): `KK4S_LAUNCHER_SHOT`, `KK4S_OVERLAY_SHOT`,
  render probes with `Layer.saveLayerImage` — see `docs/launcher.md`. Keys can be sent with
  python-xlib to the XWayland window (`SDL_VIDEODRIVER=x11`; find the window by WM_CLASS `kk4s`).
- **`SDL_RENDER_DRIVER=opengles2`** = the Switch's renderer.
- **`KK4S_SKIP=AfterInit2.tjs,Override2.tjs`** disables Kirikiroid helper scripts.
- **`tools/xp3ls`** lists XP3 contents (works on encrypted archives).
- **Probe technique** for reading decrypted game scripts: a scratch `startup.tjs` (UTF-16LE with BOM)
  that runs the game's `xp3filter.tjs`, loads a file from an archive and saves it elsewhere. Never
  commit game files.
- Profiling on PC: `eu-stack -p <pid>` in a loop (no perf on the dev box). The PC is much faster than
  the Switch; count expensive operations (file opens, texture creation) instead of timing them.

## Gotchas learned the hard way

- Many engine files use **CRLF**; edit them with Python preserving `\r\n` (or Edit), check with
  `file` and `git diff --stat`.
- TJS is not thread-safe: never run TJS off the main thread.
- KAG builds `<exe>/savedata/...` itself — engine-level path settings alone do not move saves.
- Test runs through symlinks still write into the real game folder (saves, `title.txt`): use copies
  or `KK4S_ROOT` + separate saves, and never write into the user's game folders.
- The Switch SD card is slow for many small writes / opens: log in batches, cache font faces.
- SDL's Switch controller mapping is positional (A/B swapped) — read libnx instead.
- `rm -rf` on variables is blocked by a safety check; use `"${VAR:?}"/...`.
- The user wants code reviewed before CI, not trial-and-error pushes.

## Next steps

1. Fix the FSN choice branch (flow tracker `next()` → `SELECTER`, `handlers.link`); compare the
   KAG link/choice path against the original engine behaviour, not against FSN-specific patches.
2. Hardware check of the current build: stalls after long pages (`slow frame` fields),
   exit to the launcher, L3 + R3 menu, quick load after a save made at the logo.
3. Merge the branch into `main` once the hardware check passes (CI nightly builds `main`).
4. Video playback (`krmovie.dll` → FFmpeg), e.g. openings in FSN and The Makings of a Lady.
5. Real extrans transitions (wave, ripple, mosaic, turn, rotate*) instead of crossfade.
6. Built-in x86-32 interpreter + minimal PE loader for `.tpm` decryptors, so games without
   `xp3filter.tjs` run.
7. Stalls on the Switch: whole-file decryption of big archive files on the main thread
   (`XP3Archive.cpp`) and autosave / `[tp]` pages were the suspects; decide from the next log.
8. Overlay folder (replacement files over xp3 contents) and per-game profiles (`profiles/`).
9. Input scripts on PC (`KK4S_INPUT_SCRIPT`, see `docs/debugging.md`) as the regression test for
   each game before a Switch test.
