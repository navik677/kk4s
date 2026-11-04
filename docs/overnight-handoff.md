# Overnight development handoff — 2026-11-02

## Session baseline and safety

- Repository: `C:\Users\navik\gits\kk4s`; remote `origin` is `navik677/kk4s`.
- Starting HEAD: `1f649e0cbca0fc4a1e3021df39ff066f5b2785ac` (`origin/main` at session start).
- Working branch: `work/overnight-2026-11-02`; development happened here, and `main` is advanced
  only by an explicit merge of this branch.
- Safety ref: `backup/overnight-start-2026-11-02` points to the starting HEAD.
- The branch is published to `origin` as `work/overnight-2026-11-02` with a normal fast-forward
  push; history is not rewritten.
- The starting worktree already had hundreds of modified tracked files. They were not staged,
  reverted, or included in the overnight branch's commits. A binary diff snapshot is saved outside
  the repo at `%TEMP%\kk4s-overnight-start-2026-11-02\tracked-working-tree.patch`, with the initial
  status at `%TEMP%\kk4s-overnight-start-2026-11-02\status.txt`.
- `AGENTS.md` was not found in the repository. Continue to preserve the pre-existing worktree edits;
  stage only files intentionally changed for this overnight task.

## Starting evidence

- Latest Switch ATRI log: [`test-logs/kk4s-20261102-021122.log`](test-logs/kk4s-20261102-021122.log).
- Short paired log: [`test-logs/kk4s-20261102-021153.log`](test-logs/kk4s-20261102-021153.log).
- Hardware status at start: **FAIL** after `title:xrated, 0`; previous `getNumberdId(void)` failure
  is absent, then `WIN32Dialog.setItemText` is missing (TJS exception in `win32dialog.tjs`, VM IP 12).
  This is the 2026-11-02 snapshot; the 2026-11-04 retest below supersedes it (the game now reaches
  `start.ks` and fails later on `layerEx*`).
- `k2compat.tjs:makeDummyProperty` was disassembled with the engine's bytecode dumper. Its `ENTRY`
  try/catch encloses `TYPEOFI` at VM IP 5, so the two "Invalid object context" throws are caught by
  the helper; startup continues. No engine fix is justified from these logs alone.
- Existing confirmed FSN full-playthrough on Switch predates this work. No overnight regression
  results exist until tested again.

## Work log

Update this file after each meaningful task/commit. For every game, distinguish desktop startup,
desktop gameplay, Switch build, Switch hardware startup, and Switch gameplay, using only **PASS**,
**FAIL**, **BLOCKED**, or **NOT TESTED**.

| Task | Result | Commit |
|---|---|---|
| Session bootstrap, start snapshot and work branch | Done; snapshot preserved; started on isolated branch | `06f137e` |
| Headless `WIN32Dialog.setItemText` contract + TJS probe | Desktop build and fixture passed; Switch build passed; hardware pending | `cecb5c8` |
| XP3 v2 header parsing in `xp3ls` | PASS on six NekoPara XP3 archives; data archive lists 1201 entries | `e5be02b` |
| XP3v2 `eliF` name mapping in runtime | Synthetic smoke PASS; ATRI and FSN desktop regressions PASS | `6adc443` |
| Nekopara identification and isolated desktop probe | KiriKiri Z/KAG-compatible; scheme validation FAIL; original files hash-identical | `6adc443` |
| TextRenderBase generic layout/draw implementation | Synthetic TJS PASS; ATRI opening text visibly renders; FSN regression PASS; Switch build PASS | `ee6e8f2` |
| AlphaMovie AJPM replacement | Desktop build and generated zlib-alpha AJPM smoke PASS; Switch build PASS; on hardware ATRI starts and advances, then crashes during Skip | `a448c2e` |

## Current test matrix

| Game | Desktop startup | Desktop gameplay | Switch build | Switch hardware startup | Switch gameplay |
|---|---|---|---|---|---|
| ATRI | PASS to title menu: opening Russian line renders, AlphaMovie member resolves, audio plays (ATR_sys_002/003); `Scripts.loadDataPack` now decodes the PBD layers | Desktop reaches `start.ks` and the PBD/affine scene, then fatal error on missing `layerEx*` (`clipAlphaRect`, `fillAlpha`) | PASS (2026-11-04 NRO) | PASS: enters the game, renders the opening and PBD-backed layers, then exits through the fatal-error banner | FAIL: fatal error at `start.ks:25`; next blocker is `layerEx*`, not PBD |
| Nekopara Vol. 1 | FAIL: startup TJS resolves, CX recovery 0/12; narrow-string conversion error | NOT TESTED | NOT TESTED | NOT TESTED | NOT TESTED |
| Fate/stay night, Russian patch | PASS: `data.xp3>` project path, `rupatch`/`rufix` mounted, 0 script exceptions after renderer changes | PASS (title/load menu, 2026-11-04) | PASS (2026-11-04 NRO) | PASS, user-confirmed full playthrough; 2026-11-04 log reaches the title and exits cleanly | PASS on hardware (user-confirmed) |
| Sharin no Kuni | NOT TESTED this session | NOT TESTED | NOT TESTED | PASS, previously user-tested; not re-tested | NOT TESTED this session |
| Tsukihime + Plus-Disc | NOT TESTED this session | NOT TESTED | NOT TESTED | PASS, previously user-tested; not re-tested | NOT TESTED this session |
| The Makings of a Lady | NOT TESTED this session | NOT TESTED | NOT TESTED | PASS, previously user-tested; not re-tested | NOT TESTED this session |

## Next actions

1. ATRI now enters the game on both Switch and desktop, but both fail at `start.ks:25` with
   `Member "clipAlphaRect" does not exist` (`affinelayer.tjs onPaint`) after `fillAlpha`
   (`affinesourcenoise.tjs loadImages`). Implement the generic `layerEx*` plugin contracts
   (`layerExDraw`, `layerExImage`, `layerExRaster`, `layerExBtoA`), add synthetic fixtures/tests,
   then retest ATRI and FSN. Do not call ATRI compatible based on the opening scene.
2. `Scripts.loadDataPack` is implemented and verified (see the PBD section); the remaining DataPack
   APIs (`saveDataPack`, `makeDataPackThumb`, `makeDataPackDigest`) only when a game needs them.
3. Investigate NekoPara `.tpm`/`CxFilterFS` only for a generic decoder shape/source; scheme validation
   remains 0/12. Do not add a game-specific key; preserve the separate `KK4S_ROOT` and game hashes.
4. The older ATRI crash during Skip has no crash log; keep it separate from the reproducible
   `layerEx*` fatal error above. Do not conflate them when retesting.
5. Run targeted tests/builds incrementally, max `-j4`, one full build at a time; update this handoff
   and commit verified logical tasks separately.
6. Commit reviewed work and merge it into local `main`; do not push unless the user asks.

## Findings since branch start

### Headless Win32 dialog

The ATRI wrapper's `setItemText(id, text)` resolves its symbolic ID and delegates to the native
`WIN32Dialog.setItemText`. NekoPara's original `win32dialog.dll` imports `SetDlgItemTextW`; the API
returns false when there is no dialog/control. kk4s' stub always has `hwnd == 0` and `open()` cancels,
so it now exposes that truthful failure result rather than throwing "member does not exist". The
tracked fixture `tests/tjs/win32dialog/startup.tjs` links the plugin and asserts this behavior.

- Desktop incremental build: **PASS** (CMake recompiled 180 objects because the pre-existing dirty
  worktree files were newer; only `-j4` was used). Existing warnings were non-fatal.
- Headless dialog fixture: **PASS**, log `win32dialog headless setItemText: PASS`.
- Switch build/hardware: NOT TESTED yet for this change.

### XP3v2 hashed filenames

- NekoPara's XP3v2 index uses `eliF` chunks (adlr key + UTF-16 filename) paired with `File` chunks;
  `data.xp3` has 1201 of each, including `startup.tjs`. This is a generic Kirikiri Z XP3 convention,
  distinct from kk4s/PackinOne's `hnfn` table.
- `engine/source/core/base/XP3Archive.cpp` now maps either `hnfn` or XP3v2 `eliF` names using the
  shared adlr-keyed path table; unknown/unmapped hashed entries remain excluded.
- Added `tests/xp3/elif_smoke.py`, which generates a tiny XP3v2 archive in a temporary directory and
  asserts that its `eliF`-named `startup.tjs` executes. Result: **PASS**.
- `tools/xp3ls.cpp`'s old v2 handling sought four bytes too far into the version block. Commit
  `e5be02b` fixes the offset/read bounds; all six NekoPara XP3 archives list successfully.
- Desktop synthetic XP3v2 `eliF` fixture: **PASS**. Desktop ATRI after the mapping change: **PASS to
  `title.ks:35`**, CX 12/12, no `setItemText` exception. FSN desktop regression: **PASS** with the
  `data.xp3>` project dir, `rupatch.xp3`/`rufix.xp3` mounts, and 0 script exceptions.
- Switch build: **PASS** with `SKIP_DEPS=1`, max `-j4`, in `build-switch-overnight-msys/`. The first
  attempt reused an MSYS-path cache from a UCRT shell and failed path validation; the separate MSYS
  build directory avoided modifying that cache. `out/kk4s.nro` SHA-256:
  `80C258DA0AFF106FA8CF6151552187793BABBD2F9C3E2471191E0726EDB7E346`. It was not uploaded; no
  Switch hardware result for this source exists.

### TextRenderBase generic layout/draw

- Implemented in `compat/kk4s_plugins.cpp`; added synthetic fixture `tests/tjs/textrender/startup.tjs`.
  It covers option/default propagation, measured glyph widths, wrapping/coordinates, style reset,
  ruby metadata/drawing, link color, draw dispatch, and overflow. Latest desktop result: **PASS** with
  `TextRenderBase synthetic layout/draw: PASS` and no script exceptions.
- Desktop ATRI reached `start.ks:24`; screenshot at
  `%TEMP%\kk4s-overnight-start-2026-11-02\atri-textrender-retest-screen.png` confirms the Russian
  opening line `Мир тонет.` renders. `calcShowCount(count)` was corrected from the observed bytecode
  call `calcShowCount(0)`. After advancing, `psdlayer.tjs` loads the PBD layers; the later stop is the
  missing `layerEx*` members described in the 2026-11-04 section below, not a DataPack error.
- Desktop FSN regression after renderer changes: **PASS**, `data.xp3>` project path, Russian patch
  archives mounted, 0 script exceptions.
- Switch build after this change: **PASS** at max `-j4` in `build-switch-overnight-msys/`. Latest
  `out/kk4s.nro` SHA-256: `5FD4CEB2FB8AA74472F2D2EC045792F5CD462632E6C336F89088117F33E450EE`. It was not
  uploaded; hardware startup/gameplay remain **NOT TESTED**.
- `rendermsgwin.tjs` bytecode establishes `calcShowCount(count)` as a one-number API: startup calls
  `calcShowCount(0)` then passes the result to `drawCount`. The previous guessed two-argument string
  contract caused the `start.ks:25` conversion error; the numeric clamp now passes and the first line
  renders.
- `psdlayer.tjs` bytecode shows `loadPBD(storage, options)` calls `Scripts.loadDataPack(storage,
  options)` and passes the result to `loadDATA`. When no explicit IV is supplied, options is a
  dictionary with `outeriv: PSDInfo.PBDOuterIV`. This API is now implemented; see the PBD / Scripts
  DataPack section below.

### Nekopara Vol. 1

- Located at `E:\kk4s-pc\games\NekoPara Vol.1`; this is Kirikiri Z/TJS2/KAG-compatible (PE strings
  include `kirikiriz` and `startup.tjs`; files include XP3 archives, a `.tpm`, and KAGZ plugins). The
  precise upstream engine version is not established.
- First diagnostic used stale `out-desktop/kk4s.exe` (`c0d7705`); it mounted 1201 `data.xp3` entries
  but could not find `startup.tjs`. After the generic `eliF` mapping, a fresh desktop run locates and
  attempts `startup.tjs` but exits with `Cannot convert given narrow string to wide string`; CX scheme
  recovery validates 0/12 samples. Switch is NOT TESTED.
- In both runs `compat: saves in ...` points outside the game directory; the existing five save files
  were copied to the isolated test root. SHA-256 manifests (outside Git) cover all 86 files / 3.758 GB
  before and after; **0 files changed**. The game folder is not modified.
- Initial `tools/xp3ls` probe hit `std::length_error`; its v2-header parser was reading the version
  block from the wrong offset. The generic correction now parses all six XP3 v2 archives present
  (data 1201 entries, patch 222, adultsonly 1647, emote 88, emotewin 40, voice 4845).
- After adding generic `eliF` mapping, the current desktop run locates `startup.tjs` and attempts to
  load it, but the TJS compiler reports `Cannot convert given narrow string to wide string`. The log
  shows automatic CX scheme recovery validated **0/12** samples. The original TPM is a PE32 plugin
  exporting `V2Link` and contains `CxFilterFS`; the exact format/algorithm is not yet supported by the
  generic analyzer. No game-specific key will be embedded. No game resources or saves are in Git.

### AlphaMovie contract reconnaissance

- Read-only inspection of NekoPara's `plugin/AlphaMovie.dll` confirms a native Kirikiri plugin
  (`V2Link`/`V2Unlink`) with an `AJPM` file marker and a TJS `AlphaMovie` surface including
  `open`, `clear`, `showNextImage`, `isPlaying`, `play`, `stop`, `setPosition`, `setNextMovieFile`,
  `numOfFrame`, `frame`, `loop`, `nextLoop`, `preloadSamples`, and `Layer`/screen/FPS members.
- The DLL proxies Layer properties and write buffers (`mainImageBufferForWrite`/pitch), so this is
  an alpha-frame decoder/Layer integration task, not just a filename alias. No loose `.amv` files
  were found beside the ATRI test install; archive-backed media remains to inspect. The strings-only
  reconnaissance did not itself modify code or game files; the generic implementation is recorded
  in the following section.
- Public RE lead: `xmoezzz/amv_decoder` documents AJPM's 40-byte header and packet types, but labels
  frame decode experimental; no test sample is committed. Its license is MPL-2.0. Treat it as a
  format reference, not a complete or verified decoder; any kk4s implementation needs independent
  synthetic vectors and must not import a game's `.amv` into Git.

### AlphaMovie generic replacement (committed `a448c2e`)

- Integrated the official `wamsoft/AlphaMovie` cross-platform decoder source (krkrz_dev submodule
  commit `0a910880af86800b5c6efa09bc82582ca1c06f2a`) with its Kirikiri-core license notice. It handles
  AJPM headers, FRAM chunks, both JPEG-alpha and zlib-alpha paths, and writes decoded BGRA frames to
  a Layer; `Plugins.link("AlphaMovie.dll")` now resolves to the built-in implementation.
- Added `tests/alphamovie/smoke.py` and `tests/tjs/alphamovie/startup.tjs`, which generate a synthetic
  16x16 AJPM zlib-alpha movie in a temporary directory and test header properties, frame decode,
  Layer draw dispatch, and play/stop. Result: **PASS** on desktop; no original media is included.
- ATRI was launched after integration with the current desktop binary: its `AlphaMovie` missing-member
  exception is gone, the script requests `tatinoboruawa.amv`, the opening Russian line renders, and
  the run reaches the title menu with audio (ATR_sys_002/003) and no new script exceptions beyond the
  two benign `makeDummyProperty` "Invalid object context" throws. PBD `loadDataPack` remains the
  scene-progression blocker on desktop; the screenshot alone is still not proof of decoded video
  pixels.
- Physical Switch (2026-11-02): the game starts and advances further into the story than the
  previous build, confirming the `setItemText`/TextRenderBase work on hardware. It then crashes while
  **Skip** is active. Switch gameplay is **FAIL** and full compatibility is **not** claimed; no crash
  log has been captured yet.
- Desktop build, synthetic AlphaMovie smoke, TextRenderBase, `Win32Dialog.setItemText`, and XP3v2
  `eliF` tests: **PASS**. Switch build: **PASS** (max `-j4`). Latest `out/kk4s.nro`: 27,534,757
  bytes, SHA-256 `7DC6C1E4AC19A7720E9D634AE6B7A4A7030628FBE26DE95442994A71C391C95E`, re-uploaded to
  `ftp://192.168.1.135:5000/switch/kk4s/kk4s.nro`; a server re-download hashes identically. The NRO is
  reproducible in behaviour but not byte-identical across rebuilds.

### PBD / Scripts DataPack (resolved 2026-11-04)

- ATRI `psdlayer.tjs` calls `Scripts.loadDataPack(storage, options)` from `loadPBD`; when its optional
  IV is absent it passes a dictionary containing `outeriv: PSDInfo.PBDOuterIV`, then passes the
  returned object to `loadDATA`. The consumer indexes the returned value directly: for the observed
  real packs the root is an Array whose `[0]` is `{width, height}` and whose remaining items are
  layer/group Dictionaries (`name`, `layer_id`, `group_layer_id`, `layer_type`, `left`, `top`,
  `width`, `height`, `opacity`, `visible`, `type`).
- Real ATRI `fgimage.xp3` `.pbd` files are not `UKR.5r1`; all 49 are `TJS/4s0`, `crypt=1`, `iv_len=0`.
  The format is the PackinOne data pack implemented by the MIT `xp3-brute` 0.3.7 reference:
  BLAKE2s-256 keyed by the header seed, XXH32 nonce, ChaCha8/12/20, PackinOne xorshift stream
  expansion, then 16-bit-framed LZ4 with a 64 KiB dictionary. `TJS/ns0` is the same TJS value stream
  without cipher/framing and with a final checksum.
- Implementation: `compat/pbd` (self-contained codec; no external lz4/xxhash link dependency for the
  `.pbd` path), `Scripts.loadDataPack` in `compat/kk4s_scripts.cpp`, tests in `tests/pbd`.
  All 49 real samples decode; synthetic tests cover every value type, all six crypt modes,
  dictionary LZ4, truncation and corruption. `outeriv` is ignored because every observed pack has
  `iv_len=0`; a pack that needs it would fail the type checker rather than produce garbage.
- Verified in ATRI desktop: after starting a new game, `Scripts.loadDataPack` loads
  `tatr03_m/s/w/l/ll.pbd` (all `ok`), no exception follows, and the PBD-backed character sprite and
  dialogue render. `KK4S_TRACE_DATAPACK=1` logs each successful pack load.

## Switch retest 2026-11-04 (DataPack + XP3 build)

- NRO: 27,571,621 bytes, SHA-256
  `F9F0F4D1FFA2F2299EFCC403D55F1C4E38771B603939F46EC384F21A134D82DE`, uploaded to
  `sdmc:/switch/kk4s/kk4s.nro`. Logs: `kk4s-20261104-015354.log` (ATRI),
  `kk4s-20261104-015432.log` (FSN), `kk4s-20261104-015516.log` (launcher).
- FSN (Russian patch): **PASS**. Mounts the archives, loads `first.ks` / `タイトル.ks` /
  `mirrormoon.ks`, reaches the title and exits cleanly; the user confirms it works on hardware.
- ATRI: **enters the game** (the previous title/Win32-dialog failures are gone) and renders the
  opening dialogue/sprites/BGM plus Steam achievements. At `start.ks:25` it prints its fatal-error
  banner and exits to the launcher after `Member "fillAlpha" does not exist`
  (`affinesourcenoise.tjs loadImages`) and `Member "clipAlphaRect" does not exist`
  (`affinelayer.tjs onPaint`). The log still reports `layerExDraw.dll`, `layerExImage.dll`,
  `layerExRaster.dll` and `layerExBtoA.dll` as ignored.
- Desktop reproduces the same failure with the same build (40 inputs over 54 s): three `fillAlpha`
  exceptions, then `clipAlphaRect` and the fatal-error exit. The blocker is therefore the generic
  `layerEx*` plugin API, not PBD, XP3, or Switch-specific behavior. Next cycle: implement the
  `layerEx*` contracts and retest ATRI/FSN.

## Commits and unresolved items

Overnight commits so far:

- `06f137e` — bootstrap this handoff and record the clean base/backup state.
- `e5be02b` — fix XP3v2 header parsing in `tools/xp3ls`.
- `cecb5c8` — implement the headless `WIN32Dialog.setItemText` result and TJS fixture.
- `6adc443` — map XP3v2 `eliF` names generically and add the synthetic end-to-end test.
- `ee6e8f2` — add TextRenderBase layout/drawing and its synthetic contract test (the stored subject is
  only `compat:`; the follow-up documentation commit records the task clearly).
- `e12fb32` — record verified TextRenderBase/ATRI/FSN and latest Switch build status.
- `ee6e8f2` — add the generic TextRenderBase layout/draw path and synthetic contract test.

Append new commits, exact validation outcomes, regressions, blockers, and changed files here. Do not
include game files, keys, profiles, saves, build outputs, or patched game content in Git.
