# PC viewer

Same engine as the Switch build, with `platform/desktop/` instead of libnx. It is the main
dev/test loop: crashes here are much cheaper than on the Switch.

Code parity: one `kk4s` CMake target compiles the same `platform/common/*`, `engine/source/*`,
`compat/*` and `engines/onscripter-nx/source/*` for both; `KK4S_PLATFORM` only picks
`platform/{desktop,switch}/kk4s_platform.cpp`. What differs: libnx specifics (pad, battery, romfs
fonts, `envSetNextLoad`), the SDL renderer (Windows defaults to Direct3D; run with
`SDL_RENDER_DRIVER=opengles2` for the Switch's GLES2 path) and rlvm's GL backend (EGL/ANGLE on
Windows, Mesa on the Switch).

## Run

```
kk4s <path to game folder>     # run one game directly (drag & drop a folder onto kk4s.exe works too)
kk4s                           # launcher over <data folder>/games/
kk4s --list-games              # print the folders the launcher recognises and each project dir, then exit
```

Data folder (games/, saves/, covers/, logs/, settings.ini) = folder of the executable, or `$KK4S_ROOT`.
Window: 1280x720, resizable. When a game picked in the launcher quits, kk4s restarts into the
launcher (setting); a game given on the command line just exits.

Keyboard: arrows, Enter, Esc work in games as on Windows; **F1** opens the kk4s in-game menu.
Launcher: arrows / Enter / Esc, Y or F3 = cover search, X or F2 = settings, O or F4 = game options,
mouse and wheel. PC gamepads work through SDL game controllers. See [launcher.md](launcher.md).

## RealLive (rlvm) on Windows

RealLive games run in a separate `rlvm.exe` built from the same `engines/rlvm/src` as the Switch
build (`tools/build-rlvm-win.sh`); only the platform front end and the GL backend differ. Build it,
copy `rlvm.exe` and its DLLs to `engines/rlvm/`, then run `rlvm.exe <game folder>` (or let the
launcher start it).

NWA/Ogg files are binary. Opening them in text mode corrupts the header on Windows and used to end
CLANNAD with `std::bad_alloc` in SDL's audio callback; this is fixed in the engine (see
[engines.md](engines.md)). If a RealLive game still dies right after the loading screen, run it
under gdb and check stderr for `invalid stream` (see [debugging.md](debugging.md)).

## Build

- Linux: `bash tools/build-desktop.sh` (`ASAN=1` for AddressSanitizer + UBSan; `DEPS=1` installs
  dependencies with apt). Fedora: install the packages listed in `docs/HANDOFF.md` with dnf.
  Run: `tools/run-linux.sh <game_dir>`.
- Windows: in an MSYS2 UCRT64 shell: `DEPS=1 bash tools/build-desktop.sh`. Portable MSYS2 without
  admin rights: extract `msys2-base-x86_64-latest.sfx.exe` from github.com/msys2/msys2-installer,
  run `pacman -Syu` twice, then the command above.
- Incremental rebuild: `cmake --build build-desktop -j$(nproc)`.
- Dependencies: SDL2, FreeType, FFmpeg, libpng, libjpeg-turbo (+turbojpeg), libwebp, ogg/vorbis,
  opus/opusfile, zlib, oniguruma, libcurl.
- CI (`.github/workflows/desktop.yml`) builds both; artifacts `kk4s-linux-<sha>` and
  `kk4s-windows-<sha>` (exe + DLLs).

## Status

- Linux: primary target, sanitizers + smoke test (`tests/tjs/hello`, headless) in CI; all four test
  games run locally.
- Windows: builds in CI. Engine checks for `_WIN32` / `WIN32` / `__WIN32__` were renamed to
  `KK4S_WIN32_NATIVE` (never defined), so MinGW builds the same SDL/POSIX code path as Linux and
  Switch. Differences: 16-bit `wchar_t`, Windows paths.

## Diagnostics

- `g++ -O2 -o xp3ls tools/xp3ls.cpp -lz`, then `xp3ls archive.xp3 [filter]` lists files in an XP3.
- Screenshots without screen capture and other debugging variables: [launcher.md](launcher.md).
