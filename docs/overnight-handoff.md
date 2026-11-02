# Overnight development handoff — 2026-11-02

## Session baseline and safety

- Repository: `C:\Users\navik\gits\kk4s`; remote `origin` is `navik677/kk4s`.
- Starting HEAD: `1f649e0cbca0fc4a1e3021df39ff066f5b2785ac` (`origin/main` at session start).
- Working branch: `work/overnight-2026-11-02`; do not commit to or push `main`.
- Safety ref: `backup/overnight-start-2026-11-02` points to the starting HEAD.
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

## Current test matrix

| Game | Desktop startup | Desktop gameplay | Switch build | Switch hardware startup | Switch gameplay |
|---|---|---|---|---|---|
| ATRI | PASS to `start.ks:24`; opening Russian line renders; later PBD `loadDataPack` missing | NOT TESTED (opening scene only) | PASS (latest NRO built) | NOT TESTED on latest NRO (older hardware build FAIL at `setItemText`) | NOT TESTED |
| Nekopara Vol. 1 | FAIL: startup TJS resolves, CX recovery 0/12; narrow-string conversion error | NOT TESTED | NOT TESTED | NOT TESTED | NOT TESTED |
| Fate/stay night, Russian patch | PASS: `data.xp3>` project path, `rupatch`/`rufix` mounted, 0 script exceptions after renderer changes | NOT TESTED this session | NOT TESTED | PASS, user-confirmed full playthrough on earlier build; not re-tested | NOT TESTED this session |
| Sharin no Kuni | NOT TESTED this session | NOT TESTED | NOT TESTED | PASS, previously user-tested; not re-tested | NOT TESTED this session |
| Tsukihime + Plus-Disc | NOT TESTED this session | NOT TESTED | NOT TESTED | PASS, previously user-tested; not re-tested | NOT TESTED this session |
| The Makings of a Lady | NOT TESTED this session | NOT TESTED | NOT TESTED | PASS, previously user-tested; not re-tested | NOT TESTED this session |

## Next actions

1. Investigate the generic `Scripts.loadDataPack`/`saveDataPack` PBD contract exposed after
   `start.ks:24`; use the `loadPBD` bytecode and format references, not game-specific shortcuts.
2. Investigate the NekoPara `.tpm`/`CxFilterFS` implementation and why the generic desktop scheme
   analyzer validates 0/12 samples. Do not extract, embed, or commit game-specific keys; add support
   only for a generic TPM/CX shape. Keep test data in a separate `KK4S_ROOT` and hash the original.
3. Continue through subsequent ATRI startup blockers; no title/menu success claim based only on the
   absence of an exception.
4. Run targeted tests/builds incrementally, max `-j4`, one full build at a time. Update this handoff
   and create a focused commit after each verified task.
5. Do not push to `main`; if pushing reviewed work, use only
   `work/overnight-2026-11-02`.

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
  call `calcShowCount(0)`. After advancing, `psdlayer.tjs` emits missing `Scripts.loadDataPack` errors;
  this run is not a gameplay PASS.
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
  dictionary with `outeriv: PSDInfo.PBDOuterIV`. PBD-backed content remains blocked on a generic
  `Scripts.loadDataPack`; observed per-layer exceptions are caught by KAG, not a runtime crash. The
  extracted `UKR.5r1` PBDs stay outside Git.

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
  were found beside the ATRI test install; archive-backed media remains to inspect. No implementation
  or game-file change was made from strings alone.
- Public RE lead: `xmoezzz/amv_decoder` documents AJPM's 40-byte header and packet types, but labels
  frame decode experimental; no test sample is committed. Its license is MPL-2.0. Treat it as a
  format reference, not a complete or verified decoder; any kk4s implementation needs independent
  synthetic vectors and must not import a game's `.amv` into Git.

## Commits and unresolved items

Overnight commits so far:

- `06f137e` — bootstrap this handoff and record the clean base/backup state.
- `e5be02b` — fix XP3v2 header parsing in `tools/xp3ls`.
- `cecb5c8` — implement the headless `WIN32Dialog.setItemText` result and TJS fixture.
- `6adc443` — map XP3v2 `eliF` names generically and add the synthetic end-to-end test.
- `ee6e8f2` — add the generic TextRenderBase layout/draw path and synthetic contract test.

Append new commits, exact validation outcomes, regressions, blockers, and changed files here. Do not
include game files, keys, profiles, saves, build outputs, or patched game content in Git.
