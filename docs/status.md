# Status

Last update: 2026-11-05.

## Current state (user-confirmed on Switch, 2026-11-05)

- **ATRI -My Dear Moments-** (Steam release, original folder): runs on the console.
- Other tested games (Sharin, Tsukihime, The Makings of a Lady, FSN): work; **Fate/FSN is not broken**.
- **Aspect (user-reported 2026-11-05):** Fate/FSN and Tsukihime now show **4:3 with black bars** on the 16:9 screen
  after `806395c` (letterbox, default on). Before it they filled 16:9. The user wants 16:9 for these titles.
  Planned: a 16:9 stretch or an upscaler, plus a switch between original 4:3 and 16:9 (see `docs/roadmap.md`).
- **NekoPara Vol. 1 (PC viewer, 2026-11-05, `cloud-session`): title screen, new game and the first scenes
  with the Russian patch work; A advances the text.** Three generic fixes got it there: `patch.xp3` (all entries
  share one constant adlr) is bound by file signatures (`1cd7bb3`), a PSB scenario with its chunk offset past
  the end loads (`730c6bf`), textrender's `%fface;` and friends follow KAGZ's command list (`1f9dbb6`). Earlier
  notes blamed a "`<storage>@<piece>%layer` pseudo-storage": wrong, it is a plain file name in `patch.xp3`. Not
  yet seen on the Switch; E-mote characters are not drawn (`draw()` is a stub), the font `Source Han Sans CN
  Medium` falls back to Noto Sans CJK.

## Branch `cloud-session` (2026-11-05, universal-runtime sprint)

The sprint up to `49dfe56` is merged into `main` and ran on the Switch (games work; 4:3 note above).
Session 3 on top of it (sanitizer fixes, layerExDraw, soft mode, `.tpm` interpreter, sprintf) is not
merged and not run on hardware: **35/35** normal and ASan+UBSan, NRO builds. Earlier verification: the
synthetic suite (`tools/run-tests.sh`, 25/25 normal; 24/24 ASan+UBSan before extrans) on the Linux build, and
that the Switch NRO builds (devkitA64 docker image). Commit list and details: `docs/HANDOFF.md`.

| Area | Change | Level |
|---|---|---|
| Plugins | layerExBtoA / layerExImage / layerExRaster / layerExSave native; PackinOne links all four | implemented, synthetic tests; not checked against a real game |
| Plugins | fstat.dll complete; json.dll; rest of scriptsEx.dll | implemented, synthetic tests |
| Engine | krkrs RGBA pixel order: `getMainPixel`, AlphaMovie, TLG5/TLG6 writers, `drawLine` no longer swap red/blue | implemented, tested by round trips; on-screen colour not observed |
| Engine | ASan/UBSan build runs again (GCC 13.3 miscompile in `tjsSnprintf.cpp`); palette `delete[]`, two other UB fixes | tested (sanitizer build 16/16) |
| Launcher | `data/`-only folders, `data.exe` and XP3 appended to an exe are recognised; `kk4s --list-games` | synthetic layouts tested |
| Plugins | windowEx.dll complete (monitor, metrics, environment, cursor, window rects) | synthetic tests |
| Engine | closed windows no longer receive input (UAF / frozen input after KAG's `askYesNo`); KAGParser init, ObjectList `delete[]`, hash alignment | found and verified by `tests/kag3` |
| Tests | **original KAG3 system scripts** play a scenario end to end, incl. save at a label and load back (`tests/kag3`) | normal + ASan |
| Plugins | Kirikiri Z / 2 native members (fstat + windowEx sets, `System.inputString`, `Debug.visible`, language) without `Plugins.link`; shrinkCopy, layerExAreaAverage | synthetic tests |
| Archives | keyless `.tpm` encryption discovered per archive and verified by adlr (12 families + fitted XOR formulas) | 18 synthetic schemes + 2 controls; no real encrypted game checked |
| Archives | the game's own `.tpm` filter run in a small x86 interpreter (PE32 loader, fake tp_stub exporter for both conventions, minimal Win32), bound by adlr; XOR filters decrypted natively from a per-file keystream prefix, others interpreted | `tests/tpm` (4 synthetic plugins; interpreter vs native on 4 builds of `ops.c`), ASan+UBSan; **no real game `.tpm` tried**; speed on the Switch unmeasured |
| Screen | aspect-correct scaling (letterbox, default on) with touch/mouse mapped through it | `tests/aspect`; on hardware 2026-11-05: Fate/FSN and Tsukihime show 4:3 with bars, not wanted for them; no 16:9 option yet |
| Input | Windows input rules: `onClick` left-button only, double click, wheel notch 120, button state in mouse shifts / `getKeyState(VK_LBUTTON)`, `cursorX/Y` in game coordinates, `setCursorPos` → mouse move | `tests/input` |
| Input | pad L / R = wheel (opens and scrolls the KAG3 backlog; Page Up never did), held buttons repeat like Kirikiri's pad input, pad right click at the screen centre until touched | `tests/input`, `tests/kag3/interact_smoke.py`; not tried on hardware |
| Plugins | extrans.dll transitions for real (wave, mosaic, turn, rotate*, ripple), engine internal plugin | `tests/extrans` half-way screenshots; ripple's picture unverified; not seen on hardware |
| Plugins | layerExDraw.dll (GDI+ drawing over vendored plutovg): shapes, pens, brushes, gradients, paths, text, images, transforms | `tests/layerexdraw` (GCC + clang ASan/UBSan); NRO builds; not seen in a real game or on hardware |
| Compat | optional soft mode (off by default): after an ignored plugin, missing members of native objects read / return void, logged once | `tests/softmode` (both settings); never tried with a real game |
| Engine | sanitizer run 2026-11-05: csvParser stream release, TLG5/TLG6 unaligned loads and negative shifts, psd:// pixel alignment, FreeType `FontInfo` ODR clash, ncbind null offset | whole suite 30/30 under ASan+UBSan (halt on error) |

Expected effect on ATRI (unverified): `generateWhiteNoise` no longer throws, `clipAlphaRect` really
clips affine layers (before it silently did nothing).

**CI on `main` is red since `7a77878`** (the ASan crash above; not a test failure). `348300f` fixes it
once merged.

## Builds (CI, every push to main)

| Target | State | Where to get it |
|---|---|---|
| Switch `kk4s.nro` (+ `.elf`) | builds, **runs on hardware** | [nightly release](https://github.com/navik677/kk4s/releases/tag/nightly), Actions artifact `kk4s-<sha>` |
| PC viewer, Linux (ASan + UBSan) | builds; boot smoke test + the synthetic suite (see [tests/README.md](../tests/README.md)). Red on `main` since `7a77878` (sanitizer miscompile, fixed on `cloud-session` by `348300f`) | Actions → desktop → `kk4s-linux-<sha>` |
| PC viewer, Windows (MinGW) | builds; portable decoder tests run; the viewer is **not** executed in CI | Actions → desktop → `kk4s-windows-<sha>` (exe + DLLs) |

## Tests and CI

`tools/run-tests.sh` is the single entry point; CI calls it per group, so a red
run names the component. Full list and scope: [tests/README.md](../tests/README.md).

| What | Command | CI |
|---|---|---|
| Storage: project dir, overlay precedence, XP3 parsing | `tools/run-tests.sh --group xp3 out-desktop/kk4s` | Linux, blocking |
| Engine I/O and scripts: TextStream, AlphaMovie, `Scripts.loadDataPack`, plugin contracts, input rules, KAG3 template (on `cloud-session`) | `--group io` | Linux, blocking |
| Decoders vs Python references (PBD, PSB, CXDEC) | `--group decoders` | Linux + Windows, blocking |
| Boot smoke test (`tests/tjs/hello`, 15 s under xvfb) | see `.github/workflows/desktop.yml` | Linux, blocking |

These tests boot **synthetic** games on the Linux desktop build. They prove that
storage, decoding and script-facing APIs still behave as documented; they prove
**nothing** about Switch hardware, real game content, rendering or frame rate.
Windows CI compiles the viewer and runs the decoder group only.

Fate/stay night (Russian patch) is the only real game available on this machine.
Keep the levels apart: *boots* ≠ *reaches the title* ≠ *loads a scenario* ≠
*full playthrough*. Only a hardware run supports the last one; the synthetic
suite only protects the storage rules FSN depends on.

## Tested games

All unmodified, Russian fan translations, on a real Switch (title takeover) and the Linux PC viewer.

| Game | Notes |
|---|---|
| Fate/stay night (Realta Nua, Kirikiroid2-ready folder) | User-confirmed on Switch: full playthrough from beginning to end; Russian patch, saves/load, `xp3filter.tjs`, `Override2.tjs` / `AfterInit2.tjs`; game font "Mona" Cyrillic substituted |
| Sharin no Kuni, Himawari no Shoujo | plain archives; saving, touch and gamepad checked |
| Tsukihime + Plus-Disc | own font (`umineko.ttf`) |
| The Makings of a Lady – Purity Yours to Defile | `xp3filter.tjs` (`b.xor(0,l,h)`); existing saves from the game folder are migrated |
| ATRI -My Dear Moments- (Steam release, original folder) | User-confirmed: runs on Switch (2026-11-05); `TinyFilter`/CXDEC bind 6 archives, PackinOne layers work. Affine-noise effects (`generateWhiteNoise`) are not drawn yet (round 3) |
| NekoPara Vol. 1 | **Not working, unresolved.** CXDEC decrypts and the scripts start; the title UI stops at `title.ks:13` on the KAGZ UI pseudo-storage `window@help__config%layer` (round 2–3 below) | (Corrected 2026-11-05: `window@help__config%layer` is a plain file name, `window@help__config%layer.png` in `patch.xp3`; the archive was not bound because all its entries share one constant adlr. Fixed in `1cd7bb3`.)

## Compatibility matrix (CXDEC, 2026-11-04)

Branch `compat/tinyfilter-2026-11-04`. Unmodified game folders, Linux desktop build, headless
(`SDL_VIDEODRIVER=dummy`). “Runs its scripts” means the engine executed the game's TJS/KAG after
decryption; rendering was **not** observed.

| Game | Before | After | Verified | Remaining errors | Status |
|---|---|---|---|---|---|
| NekoPara Vol. 1 | `startup.tjs` ended in `Cannot convert given narrow string to wide string`; CX recovery 0/12 | `startup.tjs` decrypts and its scripts run; 8147 files found, 7917 active | `tinyfilter/adlr-xor` bound on `data.xp3`, `voice.xp3`, `emote.xp3`, `adultsonly.xp3` (12/12, 8/8 samples); 938 real entries decrypt to `adler32 == adlr` | stops at `Member "Motion" does not exist` (`motion.tjs:28`, needs `emoteplayer.dll`); `patch.xp3` is a decoy index (222 entries, one `adlr`) and is left unfiltered on purpose | **CXDEC works; boots into its scripts, not playable** |
| Fate/stay night (Russian patch) | PASS | PASS | `project dir: …/fsn/data.xp3>`, same mounts | 0 script exceptions, 0 `cxdec` lines (no hashed-name archive) | **regression PASS** |
| ATRI -My Dear Moments- | Cx auto-recovered, stops at `start.ks:25` on missing `layerEx*` | unchanged | 6 archives bind the auto-recovered scheme (12/12, `steam.xp3` 2/2) | 2 pre-existing `k2compat` background exceptions | **no regression** (superseded by round 3) |

## Compatibility matrix, round 2: Motion, TJS `typeof`, plugin layer (2026-11-04, same branch)

Headless desktop runs (`SDL_VIDEODRIVER=dummy`); nothing here was observed with a real
window or sound output.

| Game | Before round 2 | After round 2 | Verified | Remaining | Status |
|---|---|---|---|---|---|
| NekoPara Vol. 1 | `Member "Motion" does not exist` (`motion.tjs:28`) | window is built, KAGZ environment initialises, the engine executes `title.ks` (line 13) | `Motion.*` registered and `emoteplayer.dll` linked; `typeof` of an undeclared global returns `"undefined"`; `windowEx.dll`/`menu.dll` members present | fatal `Image Load Error /Invalid image` while `buttonlayer.tjs:_copyButtonUIStates` loads `window@help__config%layer` (`kaglayer.tjs:69` → `Layer.loadImages`); emotes are not rendered (no E-mote runtime) | **starts, CXDEC + window OK, stops in the title UI** |
| Fate/stay night | PASS | PASS | `data.xp3` project dir, 0 script exceptions | — | **regression PASS** |
| ATRI -My Dear Moments- | 2 `k2compat` background exceptions | 2 (same) | Cx scheme still binds 12/12 | `layerEx*` gap unchanged | **no regression** (superseded by round 3) |

TJS change behind this: `typeof someGlobalThatDoesNotExist` used to throw `Member ... does not
exist` because a global symbol was compiled as a plain `gpd`; the compiler now asks for
`stTypeOf` there, so `VM_TYPEOFD` runs and yields `"undefined"` as in Kirikiri. Regression
assertion: `tests/tjs/hello/startup.tjs`.

Measurements (single run each, 75 s timeout, headless, one RSS sample at exit; no leak curve):
NekoPara stops after 4.2 s in the title UI (RSS 157 MB), FSN runs 75.7 s with 0 exceptions
(RSS 180 MB), ATRI runs 76.0 s with its 2 known exceptions (RSS 275 MB). Audio was not verified
(`SDL_AUDIODRIVER=dummy`).

(Corrected 2026-11-05: `window@help__config%layer` is a plain file name, `window@help__config%layer.png` in `patch.xp3`; the archive was not bound because all its entries share one constant adlr. Fixed in `1cd7bb3`.) Old analysis, kept for history: `uipsd/window.csv` maps `help_config` to `window@help__config%layer`. That is
KAGZ's UI pseudo-storage (`<storage>@<piece>%layer`), resolved on Windows by the multi-image plugin
(`multiimage.dll`, present in the game folder); kk4s hands the whole string to `Layer.loadImages`,
which tries to decode it as an image and reports `Image Load Error /Invalid image`.

Graphical mode does work on this machine (`DISPLAY=:0`, `renderer: opengl, window 1280x720`), so the
desktop viewer can be run with real video; the runs above used `SDL_VIDEODRIVER=dummy`.

## Compatibility matrix, round 3: PackinOne alpha API + long runs (2026-11-05)

Same branch, Linux desktop, real window (`DISPLAY=:0`, `renderer: opengl, window 1280x720`), audio dummy.

| Game | Before | After | Verified | Remaining | Status |
|---|---|---|---|---|---|
| ATRI -My Dear Moments- | fatal `start.ks:25` on `Member "fillAlpha"` / `clipAlphaRect` | opening plays; a 6.5-min run survived and quit cleanly (`TVPExitApplication` → `kk4s: shutdown`) | `PackinOne.dll` shim provides `Layer.fillAlpha` and `Layer.clipAlphaRect`; the game calls `fillAlpha` with 1 argument (alpha = 255); 8 exceptions in 390 s: 6 `generateWhiteNoise`, 2 known `k2compat` | `LayerExAffineSourceNoise.generateWhiteNoise` is not implemented, so affine noise layers draw nothing | **runs; user-confirmed playable on Switch** |
| Fate/stay night | PASS | PASS | 86 s, 0 exceptions, two scenes rendered (Archer at the school gate; Rin with the in-game menu in Russian) | — | **regression PASS** |
| NekoPara Vol. 1 | fatal `title.ks:13` | unchanged | stops after 5 s with 3 exceptions | `window@help__config%layer` still reaches `Layer.loadImages` (`kaglayer.tjs:69`) | **blocked, same cause** |

The 6.5-min ATRI run: RSS stayed at 333 MB for the first 5 minutes, then rose to 577 MB (peak 821 MB, measured
just after `quit`), so memory growth under load needs a longer curve before it can be called a leak. `kag.skipMode = 1`
was accepted by the game but the frame did not change: synthetic `tap` events do not advance ATRI's message layer in
this harness (the same taps do open FSN's menu), so the long run proves stability, not scenario progression.

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
- **Data packs**: `Scripts.loadDataPack` (`TJS/ns0` and PackinOne `TJS/4s0` `.pbd`) returns real TJS
  Array/Dictionary trees; decoder in `compat/pbd`, tests in `tests/pbd`.
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

- **E-mote backend:** the PSB cipher is solved and validated for NekoPara Vol.1 (157/157), but there is no
  E-mote renderer yet and no second E-mote game locally. Research and cross-game evidence:
  [emote_psb_research.md](emote_psb_research.md), [emote_crossgame_compatibility.md](emote_crossgame_compatibility.md).
  The universal runtime (`compat/emote/`: PSB core, cipher profiles, character model, animation runtime,
  renderer contract; 157/157 NekoPara PSB load) exists but is **not wired into `Motion.*` and draws
  nothing**: [universal_emote_runtime.md](universal_emote_runtime.md). NekoPara on the PC still stops after
  ~9 s (`Image Load Error` in `imageaffinesourcelayer.tjs`).
- PC games from the 2026-11-05 test round (`HANDOFF.md`, session 4): Fate, 11 eyes, Karigurashi Renai,
  Witch (Russian patch), Yosuga, Gyaru Express, Niji no Kanata ni, Chronobox, redwitch, Tsundere na Imouto
  start; Steins;Gate (RU beta) and UsotsukiGenius do not. Not checked on the Switch.
- Video: plays on PC; not checked on the Switch yet (speed of software MPEG / WMV decoding).
- The audio format converter is not flushed at end of stream: the last ~20 ms of a short sound
  (or the tail of a resampled voice) can be dropped. The 8-bit `mute.wav` is silent, so it no
  longer clicks, but short non-silent sounds at a different rate can still be cut slightly early.
- Some plugin replacements are approximations or stubs (unknown transition names fall back to crossfade,
  `firespark` / lens effects missing).
- `.tpm` decryption plugins (e.g. `cxdec.tpm`) are not run: encrypted games need an `xp3filter.tjs`.
- Universal runtime is the goal; ATRI is only the PC Steam-repack test fixture, not a product target.
  Generic `hnfn` mapping, CXDEC decoding (x86 scheme interpreter and the scheme-free `TinyFilter`
  variant), Steam compatibility and `Scripts.loadDataPack` are implemented. **Desktop recovers
  supported CX schemes at runtime** from PE/TPM data and validates them against XP3 adlr samples; the
  test ATRI folder auto-recovered 12/12 without a prebuilt scheme profile. Switch still requires a CX
  profile until the portable x86/PE analyzer is implemented. ATRI enters the game on Switch and
  desktop and, since `9f229bb`, plays the opening: the `PackinOne.dll` shim supplies
  `Layer.fillAlpha` and `Layer.clipAlphaRect`, which used to end the run at `start.ks:25`. The
  2026-11-05 desktop run lasted 6.5 min and quit cleanly; the remaining errors are
  `LayerExAffineSourceNoise.generateWhiteNoise` (6×, not implemented, so those noise layers draw
  nothing) and the 2 known `k2compat` background exceptions. `layerExDraw.dll`, `layerExImage.dll`,
  `layerExRaster.dll` and `layerExBtoA.dll` are still reported as ignored, so other members of that
  family may surface next. This does not make ATRI a product target: implement reusable KiriKiri Z
  APIs/plugins and test across unrelated games. Details: [cxdec.md](cxdec.md),
  [handoff-native-cx.md](handoff-native-cx.md).
- XP3 script filters are **not thread-safe**. `8d4bef4` routes filtered game graphics/touchimages
  through synchronous main-thread loading. User-confirmed: Fate/FSN now plays through to the end and
  other tested games work well. This hardware regression is mandatory for further filter changes.
- The game picture is letterboxed to its own 4:3 (default since `806395c`). Fate/FSN and Tsukihime look 4:3
  with bars on the 16:9 screen and the user wants 16:9 for them. Missing: a 16:9 stretch or upscaler, and a
  switch between original 4:3 and 16:9.
- Overlay folder and per-game profiles (M2/M3) are not implemented.
- Album (applet) mode memory limits not tested; docked ↔ handheld switching not tested.
- Windows build: 16-bit `wchar_t` differs from Switch/Linux.
