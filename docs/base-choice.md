# Base choice

## Chosen: `uyjulian/krkrs` (for now)

The original prompt ([original-prompt.md](original-prompt.md)) prefers `krkrsdl2`. The project owner chose to start from **krkrs**, and the sources are vendored in `engine/`.

Why krkrs is a reasonable start:
- It is already a Switch port: libnx main loop, romfs, nxlink, SDL2 window at 1920x1080, Switch-friendly storage code (`base/android/StorageImpl.cpp`).
- One CMake file, one target, all dependencies are standard switch-portlibs (+ oniguruma).
- Internal plugin table (`Plugins.link("csvParser.dll")` etc.) is exactly the "dll name → native module" model kk4s needs.

Known downsides:
- Archived, not maintained; written against devkitPro of ~2020–2021. Current GCC / FFmpeg APIs may need fixes (tracked in [status.md](status.md)).
- Switch-only: there is no `linux-sdl2` target yet, which the dev-loop plan needs. Engine code is SDL2-based, so a Linux target is mostly a matter of replacing the `switch.h` bits in `engine/source/core/sdl2/stubs.cpp` with `platform/<name>/`.
- Only unencrypted XP3, few plugins.

## Revisit point

After M1 boots a test game on hardware, compare against `krkrsdl2/krkrsdl2` again. If krkrsdl2 has much better compatibility (plugins, KAG behaviour, Linux target), moving `platform/switch/` onto it is cheap because kk4s code is isolated there. This is a big architectural fork — decide together before doing it.
