# Handoff — read this first in a new session

Last update: 2026-11-05. Goal: **a universal Kirikiri runtime/compatibility layer**: any KAG/TJS2
game folder (plain or repacked) runs unmodified on Switch and PC. ATRI is one PC-repack test fixture,
not the target. No per-game fixes, game files, keys or conversion; compatibility belongs in the engine,
`compat/` or kk4s' own files. Full requirements: `docs/original-prompt.md`; milestones:
`docs/roadmap.md`.

## Where we are (2026-11-05)

### Branch `cloud-session` (cloud session, not merged, not hardware-tested)

Work continues on `cloud-session`; `main` is untouched. Commit by commit:
- `compat/kk4s_layerex.cpp`: native **layerExBtoA / layerExImage / layerExRaster** with the real
  wamsoft contracts; `PackinOne.dll` links all three. The old PackinOne shim had a guessed
  `clipAlphaRect(x, y, w, h)`; ATRI calls the real 8-argument form (see `handoff-native-cx.md`),
  so the old shim threw inside and did nothing. `generateWhiteNoise` now draws.
- Engine pixel order: krkrs stores RGBA bytes. `Layer.getMainPixel` returned R/B swapped
  (fixed in `LayerIntf.cpp`), AlphaMovie copied BGRA frames as is (red/blue swapped in alpha
  movies, fixed), `drawLine` colour fixed. See "Pixel order" in `docs/plugins.md`.
- fstat.dll completed (listings, timestamps, attributes, MD5, TemporaryFiles); layerExSave.dll
  (PNG/TLG thumbnails, crop/diff helpers, async `Window.startSaveLayerImage`).
- Engine: TLG5/TLG6 writers swapped R/B (`Layer.saveLayerImage(..., "tlg5")` came back blue for
  red); they now read BGRA rows (`tTVPTLGSourceLines`, `SaveTLG.h`).
- Tests: `tests/layerex`, `tests/fstat`, `tests/layersave` (io group).
Retest before merging: FSN (Russian patch, `project dir: .../data.xp3>`), ATRI (affine noise /
clipped affine layers now draw; check that colours of AlphaMovie overlays look right), NekoPara.

Merged locally: `compat/tinyfilter-2026-11-04` was fast-forwarded into `main` (HEAD `721b62a`, code
from `9f229bb`). **Not pushed:** the `gh` token has no `workflow` scope, and these commits touch
`.github/workflows/desktop.yml`. Fix: `gh auth refresh -h github.com -s workflow`, then `git push`.

The SD holds the NRO built from that code (`out/kk4s.nro` in the build, 27,497,893 bytes, SHA-256
`e74d4dabe9ef13e1012337b4155a7fb0d424aec08e44e58e8d9fed427e66dc1f`). Its `.elf` is needed to symbolicate
its crash reports; keep it with the build. The user reports on hardware: **ATRI runs** and the other
tested games work, **Fate/FSN is not broken** (Fate was playable from start to end on the earlier
build); **NekoPara Vol. 1 does not work yet and is unresolved** (see `docs/status.md`).

Build the NRO without GitHub CI when needed: `podman run --rm -v "$PWD":/src:Z -w /src
docker.io/devkitpro/devkita64:latest bash -c 'git config --global --add safe.directory "*"; bash tools/build-switch.sh'`
(a fresh container needs the image pulled first and the build takes a while).

The current native repack work is engine-wide: `hnfn` names + generic CXDEC decoder/scheme binding,
Steam-build argument detection, `Scripts.loadDataPack`, and plugin compatibility. ATRI is used only to
exercise that path; it is not an acceptance target by itself. The first known remaining Switch blocker
is the `layerEx*` plugin family: `layerExDraw.dll`, `layerExImage.dll`, `layerExRaster.dll` and
`layerExBtoA.dll` are still ignored, and ATRI's affine/PSD layers call their primitives. Investigate
the missing generic API/semantics, not ATRI-specific behavior. Full status and crash analysis:
`docs/handoff-native-cx.md`, `docs/cxdec.md`.

Works on the Switch (user-tested): launcher, exit to the launcher (engine threads stopped,
detached thread stacks closed), in-game menu on L3 + R3, saves outside the game folder, FSN PC
version (only `profiles/FSN/xp3filter.tjs` needed), loading saves, choices after a loaded save,
video playback (FFmpeg player). PC viewer is the test loop; `KK4S_INPUT_SCRIPT` drives games like
a Switch user (pad, touch, screenshots, `eval`) - see `docs/debugging.md`.

### NEVER break FSN's Russian patch (learned the hard way, 2026-11-01)
FSN's Russian translation lives in `rufix.xp3` and `rupatch.xp3` next to `data.xp3`. It works
**only** because of the current project-dir/archive precedence:
- the project dir is `.../FSN/data.xp3>` (not the FSN folder);
- `rufix.xp3`/`rupatch.xp3` are mounted **after** `data.xp3` by the game's own `startup.tjs` /
  auto paths, so same-named files in them override the base ones.

What broke it last time: returning the **folder** as the project dir when the game has more than one
`*.xp3` (and, separately, mounting every `*.xp3` up front). The override order changed, the base
English files won, and the game became English. Both experiments were reverted; FSN Russian works
again on `3eed2e1`. Current `main` also has repack detection + `System.addFont`; they do not touch
the archive order, but FSN must be retested on the Switch after any merge.

Rules for any change that touches `ResolveProjectDir`, `XP3Archive`, `StorageIntf` auto paths or
startup:
1. Do **not** change the global project-dir rule or mount order to fix one game. If a game needs a
   different order, make it an explicit per-game/profile opt-in (and test FSN first).
2. Do **not** mount all `*.xp3` in `BeforeStartup` for all games.
3. Gate new archive/startup behaviour narrowly (e.g. only when the game has `hnfn` records, or
   only when `DetectGameIssue()` finds Steam/PackinOne markers). Check that FSN's folder does not
   match the gate.
4. Regression test after every such change: launch FSN, confirm the log says
   `project dir: .../FSN/data.xp3>` (not the folder), and the title/menu and the first scenario
   line are in Russian. If it is English, revert before doing anything else.

The native PackinOne WIP (`wip/packinone-native`) follows this: `hnfn` name mapping and the magic
XOR only activate when the archive contains `hnfn` records, and the KiriKiri Z startup shim only
when `DetectGameIssue()` flags the folder (steam_api + ds.ini/SteamEmu/PackinOne/.xp3.index).
FSN has none of those, but it must still be tested on the Switch before merging the branch.

### Audio clicks on the Switch: fixed 2026-11-01
Clicking A while a voice line plays (skipping it) gave an audible "tap" on the Switch.
- Cause: FSN plays `system/mute.wav` when a voice is stopped. It is 8-bit PCM
  (22050 Hz mono). Kirikiri supplies 8-bit PCM as **unsigned** (WAV/DirectSound
  convention; `TVPConvertPCMTo16bits` subtracts 0x80), but the SDL mixer told
  SDL the format was `AUDIO_S8`. The file's near-silence (0x7E/0x7F) became
  +32256/+32512, i.e. a full-scale positive DC step for ~84 ms, cut off when
  the stream ran dry: the "tap".
- Fix: `CreateStream` uses `AUDIO_U8` for 8-bit sources (`WaveMixer.cpp`); SDL
  converts it to the device's signed 16-bit correctly. Confirmed on the Switch.
- Also found while diagnosing: the engine's `mute.wav` stream runs dry because
  SDL's resampler keeps ~20 ms of the final short append in the stream (it is
  never flushed). It is silent, so it no longer clicks; a flush would be
  needed for short non-silent 8-bit/lower-rate sounds to play to the last sample.

### Universal engine compatibility / repacks

The product goal is **any unmodified Kirikiri/KAG/TJS2 game folder**, not ATRI. ATRI is a single
PC-Steam-repack test fixture for the generic `hnfn` + CXDEC + Steam-loader path. Do not add ATRI
paths, keys, offsets, names or behavior. Every compatibility fix must implement a reusable KiriKiri
API/semantic or a generally detectable plugin/platform behavior; test against the existing game set.

- Generic native repack path is implemented: `hnfn` names, CXDEC decoder, and verified scheme binding.
  `Scripts.loadDataPack` decodes real PackinOne `TJS/4s0` `.pbd` packs. ATRI's original folder
  enters the game on Switch and desktop. The 2026-11-04 NRO stopped at `start.ks:25` on
  `Member "clipAlphaRect" does not exist` (`affinelayer.tjs onPaint`, after `fillAlpha` in
  `affinesourcenoise.tjs loadImages`); `9f229bb` supplies both through the `PackinOne.dll` shim, so
  the opening now runs. A 6.5-min desktop run (2026-11-05) stayed alive and quit cleanly; the only
  remaining ATRI errors are 6 `LayerExAffineSourceNoise.generateWhiteNoise` calls (not implemented,
  so affine noise layers draw nothing) and the 2 known `k2compat` background exceptions. Still a test
  fixture, not a target.
- Steam detection (`steam_api.dll` plus `steam.tjs`/`steam.xp3`), `-steam=yes`, KZ release
  `-debugwin=no`, and the `krkrsteam.dll` compatibility object are in the engine layer. Achievements
  are logged, cloud is unavailable.
- The current `textrender.dll` replacement is a no-op; it is not a completed renderer and must not be
  mistaken for a playable-game milestone. Implement text rendering and remaining generally useful
  KAGZ APIs/plugins (`layerEx*`, `AlphaMovie`, etc.) as needed by more than one game.
- One-time ATRI extraction is only a diagnostic/reference route (`docs/repacks.md`), never a user
  requirement or acceptance path.
- **Never** change `ResolveProjectDir` or global XP3 mount order for one title. FSN Russian depends on
  `project dir: .../FSN/data.xp3>` and `rupatch.xp3`/`rufix.xp3` overlay precedence (rules above).

### Other known issues
- Slow frames on page changes (Switch): 300-900 ms, almost all "other" (game scripts / layer ops
  in TJS). Not yet profiled further.
- "Text appears at once" in FSN = the game's own setting (already-read text speed = instant);
  unread text types normally. Not a bug.
- `.tpm` decryption not implemented: encrypted games need a Kirikiroid2 `xp3filter.tjs`
  (game folder or `<data>/profiles/<game>/`).
- Video: wall-clock sync (not audio clock); mixer mode drawn like overlay; not speed-checked for
  WMV/H.264 on the Switch.
- Quick-load hook trimming (`data900.kdt.kk4s`) is count-based (approximate).

### What was done in the 2026-10-31/11-01 sessions (details in `docs/status.md`)
Exit to launcher (libnx exit mode, engine shutdown, `TVPStopAllThreads`, `__syscall_thread_detach`
reaping, pausing leftovers), L3 + R3 menu, worker threads on cores 1-2, CPU boost setting, -O3
pixel loops, fonts in memory, 64 KB write buffer, slow-frame breakdown, old Kirikiri 2 compat
(exe options, `-evalcontext=global`, class vars keeping native properties), util.dll / fstat.dll,
profiles folder for helper scripts, quick-load hook trimming, KAG call-stack restore fix (choices
after loading), video player + anti-hang, audio resampling and fade-out, 8-bit PCM unsigned fix
(the "tap" when skipping a voice), input scripts with `eval`.

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
| `docs/repacks.md` | protected/repacked games: `hnfn` names, the XOR layers, the verified one-time extraction procedure |
| `docs/packinone-native.md` | native PackinOne support: WIP branch, CXDEC evidence/key runs, where it stopped, next steps |
| `tools/` | `build-switch.sh`, `build-desktop.sh`, `run-linux.sh`, `ci-report.sh`, `resolve-crash.py`, `xp3ls.cpp`, `tjsdis.py` (TJS2 bytecode disassembler) |

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

### Regression suite (before touching storage, startup or decoders)

```bash
bash tools/run-tests.sh                  # all groups against out-desktop/kk4s
bash tools/run-tests.sh --group xp3      # project dir + overlay precedence
bash tools/run-tests.sh --group io       # TextStream, AlphaMovie, loadDataPack
bash tools/run-tests.sh --group decoders # PBD, PSB, CXDEC vs Python references
```

`tests/xp3/precedence_smoke.py` pins the two rules FSN depends on (project dir
and last-mounted-wins overlay order, with the current dir first). If it fails,
treat that as an FSN regression: do not relax the test to match the new
behaviour without re-checking FSN on hardware. The suite runs synthetic games on
the desktop build and never replaces the hardware check — see
[tests/README.md](../tests/README.md) for what it does and does not cover.

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
  Audio: `sound: open/start/stop <ptr> '<file>'` ties a mixer stream to its file, `audio: new stream`
  shows the source format, `audio: jump` flags a large sample step in the mixed output (a click),
  `audio: stream … ran dry` and `audio: late callback` flag underruns.
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

- krkrs layer pixels are **RGBA bytes** (uint32 `0xAABBGGRR`), not Kirikiri's BGRA. Native code on
  `mainImageBuffer` must swap R/B (`kR`/`kB` in `compat/kk4s_layerex.cpp`, `TVP_REVRGB`).
- Many engine files use **CRLF**; edit them with Python preserving `\r\n` (or Edit), check with
  `file` and `git diff --stat`.
- TJS is not thread-safe: never run TJS off the main thread.
- KAG builds `<exe>/savedata/...` itself — engine-level path settings alone do not move saves.
- Test runs through symlinks still write into the real game folder (saves, `title.txt`): use copies
  or `KK4S_ROOT` + separate saves, and never write into the user's game folders.
- The Switch SD card is slow for many small writes / opens: log in batches, cache font faces.
- Never change `ResolveProjectDir` (folder vs `data.xp3>`) or the global XP3 mount order to fix one
  game: FSN's Russian patch (`rufix.xp3`/`rupatch.xp3`) depends on the current precedence and the
  game became English when the project dir was the folder. If a game needs another order, make it an
  explicit per-game/profile opt-in and test FSN plus ATRI afterwards.
- SDL's Switch controller mapping is positional (A/B swapped) — read libnx instead.
- `rm -rf` on variables is blocked by a safety check; use `"${VAR:?}"/...`.
- The user wants code reviewed before CI, not trial-and-error pushes.

## Next steps (universal engine; ATRI is one test fixture)

1. Implement the remaining generic plugin APIs named by the logs: the KAGZ UI pseudo-storage
   `<storage>@<piece>%layer` that stops NekoPara at `title.ks:13`, and `layerExDraw.dll`
   (layerExBtoA / layerExImage / layerExRaster are done on `cloud-session`). Do not special-case a game;
   implement the plugin contracts (see `docs/plugins.md`), add focused fixtures/tests, then retest
   ATRI, NekoPara and FSN.
2. Implement the real generic `TextRenderBase` behavior (the API surface exists but `textrender.dll`
   draws nothing), then add other KAGZ API/plugin compatibility only when evidence from multiple
   games supports it.
3. Broaden the regression suite across unrelated KAG/TJS2 games and language patches. Keep the
   confirmed Fate/FSN full playthrough and FSN Russian patch as mandatory regressions. All changes
   belong in engine/compat, never in game files or title-specific branches.
4. `.tpm` decryptor support (own x86 interpreter + PE loader), so encrypted games do not need an
   `xp3filter.tjs`. Desktop CX schemes now recover at runtime from supported PE/TPM formats; Switch
   still needs the portable x86/PE recovery path (until then a scheme profile is required there).
5. Profile 300-900 ms Switch slow frames; implement real extrans transitions and remaining plugins
   as broadly useful. Video audio-clock sync, per-game profiles/overlay folder, and flushing the final
   ~20 ms from the audio converter remain open.

Rules from the user: test on the Switch before merging; everything must work for any Kirikiri
game, not just FSN; never commit game files or keys; game folders are never modified.

## Native Cx/PackinOne work (merged from `wip/packinone-native`, 2026-11-01)

CXDEC decoding, Steam compatibility, PSB/psbfile, KAG/Scripts APIs and debugging aids are in `main`.
Desktop now auto-recovers supported CX schemes from generic PE/TPM structures without a pre-made
scheme profile (ATRI test: 12/12 validation); Switch still needs a profile pending the portable x86
analyzer. ATRI confirms the PC scheme recovery path but remains only a test fixture: on Switch and
desktop it now enters the game, and since `9f229bb` the PackinOne `fillAlpha`/`clipAlphaRect` shim
carries it through the opening (6.5-min desktop run, clean quit). What is left is generic:
`LayerExAffineSourceNoise.generateWhiteNoise`, the rest of the ignored `layerEx*` family, and the KAGZ
`%layer` pseudo-storage. FSN was re-checked the same day (86 s, 0 exceptions). User-confirmed
regressions: Fate/FSN and the other tested games
work on Switch; Fate has a complete beginning-to-end playthrough. The FSN TJS-filter crash was traced
to running its XP3 filter on the async image thread; `8d4bef4` routes filtered graphic loading through
the main thread. Full details, known limits and remaining generic compatibility gaps are in
`docs/handoff-native-cx.md` and `docs/cxdec.md`. Never optimize for ATRI at the expense of the
universal compatibility goal.
