# Task: Kirikiri (KAG/TJS2) VN runtime for Nintendo Switch homebrew

## Goal
Build a Nintendo Switch homebrew (.nro) that runs many different Kirikiri2/Kirikiri Z visual novels **unmodified**: the user puts the original game folder (xp3 archives, data.xp3, patch.xp3, startup.tjs, etc.) on the SD card, picks it in a launcher, and it just runs. No per-game patching of game files. All compatibility work lives in the engine and in external per-game profiles.

Users supply their own legally owned game files. The repo must contain **no game assets, no copyrighted data, no bundled game keys that ship a specific title's content**.

## Base
1. Start by evaluating two bases and pick one, explaining why (short report in `docs/base-choice.md`):
   - `uyjulian/krkrs` (archived, Switch-specific, legacy)
   - `krkrsdl2/krkrsdl2` (SDL2, actively maintained, multiplatform)
2. Strongly prefer krkrsdl2 + a Switch platform layer (devkitPro/libnx/SDL2 portlibs) unless krkrs is clearly better. Keep platform code isolated in `platform/switch/` so upstream merges stay easy.
3. Toolchain: devkitPro (devkitA64, libnx, switch-portlibs). Provide a reproducible build: `Dockerfile` based on `devkitpro/devkita64` + `make`/CMake script producing `krkr-switch.nro`. CI via GitHub Actions.

## Architecture (implement in this order)
### M1: Boots and renders
- Builds clean for **two targets from the same codebase**: `linux-sdl2` (primary dev/test loop) and `switch` (.nro). Launches on real Switch hardware; emulator is optional and only for smoke tests of libnx init, SD paths and input.
- Opens a window at 1280x720 handheld/1920x1080 docked, scales the game's native resolution with aspect-preserving letterbox.
- Loads a plain unencrypted xp3 and runs `startup.tjs`. Logging to `sdmc:/switch/krkr/logs/`.

### M2: Storage layer
- Virtual FS: xp3 reading, directory reading, **overlay directory** on SD (`sdmc:/switch/krkr/overlay/<game_id>/`) that takes priority over xp3 contents, so fixes never touch original files.
- Correct handling of Shift-JIS/UTF-16 file names inside xp3, case-insensitive lookup, path normalization (`file://./`, `archive>file`).
- Memory-bounded streaming and cache with configurable limits. Must behave in both applet mode and title takeover (query available memory, scale caches).

### M3: Game profiles and detection
- Detect a game by hash of key files (xp3 headers, exe if present, startup.tjs) and load `profiles/<game_id>.json` from SD or the app romfs.
- Profile schema (document it in `docs/profile-schema.md`): display resolution, forced fonts, required plugin set, xp3 filter/crypto module id and parameters, known quirks flags, input mapping overrides, video mode.
- Unknown game → run with defaults and write a diagnostic report listing which plugins/classes/crypto were missing, so the user can submit a profile.

### M4: Plugin shim (replaces Windows DLLs)
- `Plugins.link("xxx.dll")` resolves through a table `dll name → native module`, never loads a real DLL.
- Implement incrementally, in order of how common they are: wuvorbis, krmovie/wmv shim, layerExDraw, layerExImage, layerExAlpha, ncbind, extrans, fstat/csvParser/menu/scriptsEx stubs, win32dialog/kagex stubs. Unimplemented plugin → clear log line + safe no-op stub where the game tolerates it.
- Each plugin lives in `src/plugins/<name>/` with a test.

### M5: xp3 encryption support
- Pluggable xp3 filter interface (`Xp3Filter::decrypt(offset, buf, len, params)`).
- Implement the generic, well-documented filter types used by Kirikiri games; select via profile. Keys/params come from the user's profile files on SD, **not** hardcoded for specific commercial titles in the repo.

### M6: Core compatibility
- Make `System`, `Window`, `Storages`, `Date`, `Debug`, `Scripts`, `Layer`, `Timer`, `Pad`, `WaveSoundBuffer` behave like the Windows build. Add a TJS2/KAG conformance test suite (small synthetic scripts, no game content) under `tests/`.
- Engine-level shims: ability for a profile to attach TJS/KAG hooks (monkey-patch a class method at runtime) when a game relies on a bug or odd behavior. Hooks are stored in the profile or overlay, never in game files.
- Fonts: bundle a free CJK fallback font (Noto Sans CJK or similar, check license) and map requests for MS Gothic/MS Mincho/etc. to it, configurable per profile.

### M7: Video and audio
- Video via ffmpeg/libav (check what's available in switch-portlibs; otherwise a minimal decoder path), including alpha-channel movies for layered video. Hardware decode via NVDEC if feasible, software fallback.
- Audio: ogg/vorbis, wav, opus; low-latency SDL2 audio; correct volume/fade/loop semantics.

### M8: Input and UX
- Gamepad → KAG key events (confirm, cancel, skip, auto, backlog, menu), touch → mouse (tap, drag, long-press = right click), configurable per profile.
- Launcher (SDL2-based): scans `sdmc:/switch/krkr/games/`, shows title/cover from an optional `meta.json` in the game folder, remembers last played.
- Per-game save directory `sdmc:/switch/krkr/saves/<game_id>/` (redirect `savedata`/`data` writes there), suspend/resume safe, autosave on home-button exit where possible.
- In-game overlay menu (hold a button combo): scale mode, text speed, volume, toggle debug log, return to launcher.

## Dev loop and debugging (important: the target device crashes Atmosphere often)
The developer's Switch crashes frequently during development, so **minimize time spent on hardware**. Build the project around this:

1. **Linux-first development.** Everything platform-independent (TJS2/KAG, VFS, xp3, filters, profiles, plugins, layer compositing) must be buildable and runnable on desktop Linux via the `linux-sdl2` target. Provide `make linux` / `make linux-asan` and a script `tools/run-linux.sh <game_dir>`. Platform-specific code lives only behind a small interface in `platform/<name>/` (window, audio, input, storage paths, memory query, video decode).
2. **Sanitizers by default on Linux.** `linux-asan` target builds with `-fsanitize=address,undefined -fno-omit-frame-pointer -g`. CI runs the synthetic test suite under ASan/UBSan on every commit. Fix every report; treat UB that "works on x86" as a bug since it tends to crash on Switch.
3. **Crash-surviving logging.** Logger writes to `sdmc:/switch/krkr/logs/krkr-<timestamp>.log` with `fflush` + `fsync` after every line (configurable level, default info; a `--verbose` / profile flag for trace). The last line before a crash must be on disk. Also log a build id (git hash) on startup.
4. **nxlink support.** Mirror log output to stdout and call `nxlinkStdio()` when launched via `nxlink -s`, so logs stream over Wi-Fi without touching the SD.
5. **Crash analysis workflow.** Always build the Switch target with `-g` and keep the `.elf` next to each `.nro` (and archive it in CI artifacts). Document in `docs/debugging.md` how to read Atmosphere crash reports from `sdmc:/atmosphere/crash_reports/` and resolve addresses with `aarch64-none-elf-addr2line -e krkr-switch.elf <addr>`. Provide `tools/resolve-crash.py` that takes a crash report + elf and prints a symbolized backtrace.
6. **Memory budget.** Central allocator wrapper with a hard configurable budget, counters per subsystem (images, audio, scripts, caches) and a clear logged error instead of silent OOM. Query available memory at startup (applet mode has far less than title takeover) and size caches from it. Add a debug overlay/log line showing current usage.
7. **Stack safety.** The TJS2 interpreter is deeply recursive: create interpreter/worker threads with explicit large stacks on Switch, add a recursion depth guard with a clear error, and test with deeply nested synthetic scripts.
8. **Dev run mode.** Document launching from hbmenu in title takeover mode (hold R while launching a game) for development, and testing applet mode (album launch) separately as a dedicated checklist item, since memory behavior differs.
9. **Hardware test checklist.** Maintain `docs/hardware-test-checklist.md` listing only what truly needs real hardware (applet vs takeover memory, performance, NVDEC/video, touch, audio latency, suspend/resume, docked/handheld switching), so hardware sessions are short and targeted.

## Rules
- Work in small commits per milestone; every milestone must build and have a short `docs/` note on what works and what doesn't.
- Never modify or require modification of the user's game files. Fixes = engine code, profile JSON, or overlay.
- Keep upstream-derived code and licenses intact; add `THIRD_PARTY.md` listing every dependency and license.
- No game assets, copyrighted text, or title-specific decryption keys in the repo. Examples and tests use synthetic data only.
- When something requires hardware testing I can't see (real Switch), say so explicitly, add it to the hardware checklist, and add a debug build flag that dumps useful logs to SD instead of guessing. Prefer verifying on the Linux target first.
- Ask me before big architectural forks (switching base repo, adding a large dependency). Otherwise decide and proceed.

## First step
Don't write code yet. Inspect both repos, check their build state against current devkitPro, and give me: (1) chosen base with reasons, (2) a concrete dependency list, (3) the milestone plan with estimated risk per milestone, (4) the exact Docker/build setup you'll use. Wait for my OK, then start M1.
