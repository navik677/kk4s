# Status

Last update: 2026-11-01.

## Builds (CI, every push to main)

| Target | State | Where to get it |
|---|---|---|
| Switch `kk4s.nro` (+ `.elf`) | builds, **runs on hardware** | [nightly release](https://github.com/navik677/kk4s/releases/tag/nightly), Actions artifact `kk4s-<sha>` |
| PC viewer, Linux (ASan + UBSan) | builds, smoke test passes (hello game 15 s under xvfb) | Actions → desktop → `kk4s-linux-<sha>` |
| PC viewer, Windows (MinGW) | builds | Actions → desktop → `kk4s-windows-<sha>` (exe + DLLs) |

## Tested games

All unmodified, Russian fan translations, on a real Switch (title takeover) and the Linux PC viewer.

| Game | Notes |
|---|---|
| Fate/stay night (Realta Nua, Kirikiroid2-ready folder) | User-confirmed on Switch: full playthrough from beginning to end; Russian patch, saves/load, `xp3filter.tjs`, `Override2.tjs` / `AfterInit2.tjs`; game font "Mona" Cyrillic substituted |
| Sharin no Kuni, Himawari no Shoujo | plain archives; saving, touch and gamepad checked |
| Tsukihime + Plus-Disc | own font (`umineko.ttf`) |
| The Makings of a Lady – Purity Yours to Defile | `xp3filter.tjs` (`b.xor(0,l,h)`); existing saves from the game folder are migrated |

## Working

- **Launcher** with covers (carousel / grid), VNDB cover search, settings, game options, save
  backups, autostart, 4 UI languages — see [launcher.md](launcher.md).
- **In-game menu** (L3 + R3 together; F1 on PC): quick save / load, volume, controls, exit to the launcher.
- **Input**: Switch buttons via libnx by label, configurable per button; touch = left mouse button;
  keyboard → Windows VK codes; `System.getKeyState` (skip with Ctrl held).
- **Saves** outside the game folder (`<data>/saves/<game>/`), migrated on first start.
- **Kirikiroid2 conventions**: `xp3filter.tjs`, `Storages.setXP3ArchiveExtractionFilter`,
  `Override2.tjs`, `AfterInit2.tjs` — see [kirikiroid-compat.md](kirikiroid-compat.md).
- **Plugins**: native replacements for common DLLs — see [plugins.md](plugins.md).
- **Fonts**: game-folder and system fonts (Nintendo shared fonts on Switch), fallback for unknown
  names, proportional Cyrillic for Japanese fonts that draw it full-width, face cache.
- **Logs**: one per run, written by a background thread; engine script errors with TJS stack traces.

## Engine fixes (all marked `kk4s:` in `engine/`)

- Switch threads: libnx starts every new thread on core 0 next to the main thread, at a lower
  priority, so audio decoding, the mixer, timers and image loading only ran while the main thread
  waited (stutter, slow frames). Engine threads, the SDL audio thread and kk4s' own threads now move
  themselves to cores 1–2 (`kk4s::PlaceWorkerThread`); the mixer gets a higher priority. The main
  loop sleeps 1 ms when it presented nothing instead of spinning.
- Exit (in-game menu, script errors) went straight to `exit()`, skipping the engine shutdown;
  with libnx' default exit mode the engine threads kept running inside the next NRO and corrupted
  its heap (crash in the launcher after a game). `TVPExitApplication` now runs `TVPSystemUninit`,
  then `TVPStopAllThreads` (sound decoder threads and the image loader are not ended by the
  shutdown), and the Switch pauses anything left (`PauseTrackedThreads`), then `_exit`.
- Font files are read into memory once (`FreeType.cpp`, `kk4sFontImages`): FreeType read glyph data
  on demand in small seek + read steps, i.e. many SD card requests per new glyph on the Switch.
- Engine options embedded in the game exe are read; `-evalcontext=global` (or an exe older than that
  option) makes `!` run on the global object, as Kirikiri 2.19 did (FSN PC version without
  `AfterInit2.tjs`).
- Old Kirikiri 2 (exe without -evalcontext): a class `var x;` keeps a native property `x`
  (`TJSClassVarKeepsProperty`). Old KAG3 MessageLayer has `var face;` and draws with
  `face = dfProvince`; without this FSN's message layer turned black and links were unreadable.
- libnx has no `pthread_detach`: kk4s defines `__syscall_thread_detach` and closes ended detached
  threads (their stacks stayed mapped over the heap: launcher crash after a game).
- util.dll (`enabledIME`, `enumFont`, ...) and fstat.dll (`Storages.fstat`, `copyFile`, ...) added.
- Video: `VideoOverlay` had no player at all (krkrs disabled the whole open path), so videos were
  black and `[wv]` could wait forever. kk4s now plays them with FFmpeg (`movie/kk4s_videoplayer.cpp`):
  overlay mode drawn over the game by the presenter, layer mode copied into the layers; tested on PC
  with FSN's op01.mpg (MPEG-1 + MP2) and a generated clip.
- KAG parser: a call-stack entry restored from a save without `ExcludeLevel` / `IfLevel` (saves
  from other Kirikiri versions, e.g. FSN's own) became "excluding at level 0", so after `[return]`
  everything to the end of the file was skipped (FSN: choices never appeared after loading).
  Missing values now mean "not excluding". `RestoreIntStackFromStr` read `str[i+n]` instead of
  `str[i*8+n]`, breaking nested `[if]` state in saves.
- Audio clicks on the Switch: the mixer converted each appended chunk with its own SDL_AudioCVT,
  so the resampler restarted at every chunk edge (~55 jumps per second in a 44.1 kHz sine played at
  48 kHz; the Switch always outputs 48 kHz). It now uses one SDL_AudioStream per sound buffer: no
  jumps in the same test (`WaveMixer.cpp`).
- A sound stopped while playing (voice skipped with A) ended mid-wave: a click. The mixer now plays
  the next ~8 ms of it fading to silence (`tTVPSoundBuffer::CaptureTailLocked`).
- 8-bit PCM is unsigned in Kirikiri (WAV/DirectSound convention), but the mixer told SDL it was
  `AUDIO_S8`. FSN's `system/mute.wav` (played when a voice is stopped) is 8-bit near-silence
  (0x7E/0x7F), so it became a full-scale +DC step for ~84 ms: the "tap" heard on every skipped
  voice. 8-bit sources now use `AUDIO_U8`. Confirmed on the Switch.
- Audio diagnostics in the log: `sound: open/start/stop <ptr> '<file>'`, `audio: new stream` (source
  format), `audio: jump` (large sample step in the mixed output), `audio: reset/pause/adopt`,
  `ran dry` and `late callback`. They stay in release builds (marked `kk4s:`); the caps keep the
  log small.
- Pixel loops (`tvpgl.cpp`, `gl/blend_function.cpp`, `gl/ResampleImage.cpp`) are built with -O3:
  GCC vectorizes 135 instead of 59 loops in `blend_function.cpp` and 77 instead of 12 in `tvpgl.cpp`
  for aarch64 (NEON).
- Local files are written with a 64 KB stdio buffer (newlib's default is 1 KB, i.e. one file system
  request per KB on the SD card).
- Relative storage names inside `data.xp3>` were never found (krkrs prefixed every name with `file:/`).
- Case-insensitive file lookup on case-sensitive file systems; when creating folders the existing
  part keeps its real spelling (a lower-cased copy of the whole path was created before).
- TJS xp3 filters ran on the audio thread while the main thread ran scripts; TJS is not thread-safe
  (random crashes). With a script filter set, archive files opened on the main thread are decrypted
  completely there. Non-main threads also have their own register stack and never touch the shared
  stack refcount.
- Closed windows (e.g. the save dialog) keep `TJSNativeInstance == NULL` until deleted: checked in
  input dispatch.
- Font faces were recreated on every font switch (slow on the SD card): cached.
- Touch: event cases fell through (one tap = two clicks); touch → mouse hints had wrong names.
- SDL keycodes are translated to VK codes; `TVPGetKeyMouseAsyncState` works.
- Engine log (`TVPAddLog`) goes to the kk4s log file.
- FFmpeg audio decoder ported to the send/receive API; TLG5 compressor buffer overflow; system
  headers inside `namespace TJS`; missing `return`; MinGW path shims.

## Not done / not checked

- Video: plays on PC; not checked on the Switch yet (speed of software MPEG / WMV decoding).
- The audio format converter is not flushed at end of stream: the last ~20 ms of a short sound
  (or the tail of a resampled voice) can be dropped. The 8-bit `mute.wav` is silent, so it no
  longer clicks, but short non-silent sounds at a different rate can still be cut slightly early.
- Some plugin replacements are approximations or stubs (extrans transitions fall back to crossfade,
  `firespark` / lens effects missing).
- `.tpm` decryption plugins (e.g. `cxdec.tpm`) are not run: encrypted games need an `xp3filter.tjs`.
- Universal runtime is the goal; ATRI is only the PC Steam-repack test fixture, not a product target.
  Generic `hnfn` name mapping + CXDEC decoding and Steam compatibility are implemented. Desktop
  ATRI reaches title setup, but the user confirms it still does **not** reach its title menu on
  Switch; latest log reaches `title:xrated, 0` then fails in the KAGZ Win32-dialog wrapper
  (`getNumberdId` receives `void`). `textrender.dll` remains a no-op, so this is not a playable-game
  milestone. Next work is the generic KiriKiri Z dialog/text compatibility layer, validated across
  multiple games—not an ATRI-specific workaround. Details: [cxdec.md](cxdec.md),
  [handoff-native-cx.md](handoff-native-cx.md).
- XP3 script filters are **not thread-safe**. `8d4bef4` routes filtered game graphics/touchimages
  through synchronous main-thread loading. User-confirmed: Fate/FSN now plays through to the end and
  other tested games work well. This hardware regression is mandatory for further filter changes.
- The game picture is stretched to the whole screen (no aspect-correct scaling / letterbox yet).
- Overlay folder and per-game profiles (M2/M3) are not implemented.
- Album (applet) mode memory limits not tested; docked ↔ handheld switching not tested.
- Windows build: 16-bit `wchar_t` differs from Switch/Linux.
