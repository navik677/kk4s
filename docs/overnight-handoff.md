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
| Headless `WIN32Dialog.setItemText` contract + TJS probe | Desktop build and fixture passed; Switch build pending | pending |
| XP3 v2 header parsing in `xp3ls` | PASS on six NekoPara XP3 archives; data archive lists 1201 entries | pending |
| Nekopara identification and isolated desktop probe | KiriKiri Z/KAG-compatible; engine startup still fails before gameplay; original files hash-identical | pending |

## Current test matrix

| Game | Desktop startup | Desktop gameplay | Switch build | Switch hardware startup | Switch gameplay |
|---|---|---|---|---|---|
| ATRI | PASS to `start.ks` in prior desktop run; not re-run | NOT TESTED | PASS for starting NRO build | FAIL at missing `WIN32Dialog.setItemText` | NOT TESTED (startup fails before gameplay) |
| Nekopara Vol. 1 | FAIL: current desktop runtime cannot find `startup.tjs` after mounting `data.xp3` (1201 entries) | NOT TESTED | NOT TESTED | NOT TESTED | NOT TESTED |
| Fate/stay night, Russian patch | PASS in prior PC regression; not re-run | NOT TESTED this session | NOT TESTED | PASS, user-confirmed full playthrough on earlier build; not re-tested | NOT TESTED this session |
| Sharin no Kuni | NOT TESTED this session | NOT TESTED | NOT TESTED | PASS, previously user-tested; not re-tested | NOT TESTED this session |
| Tsukihime + Plus-Disc | NOT TESTED this session | NOT TESTED | NOT TESTED | PASS, previously user-tested; not re-tested | NOT TESTED this session |
| The Makings of a Lady | NOT TESTED this session | NOT TESTED | NOT TESTED | PASS, previously user-tested; not re-tested | NOT TESTED this session |

## Next actions

1. Inspect why the located KiriKiri Z/KAG-compatible NekoPara install has no accessible
   `data.xp3>startup.tjs`; distinguish archive/header limitations from missing script content. Keep
   user data under a separate `KK4S_ROOT` and verify original hashes after every run.
2. Build/test the headless `WIN32Dialog.setItemText` change on Switch; the desktop fixture currently
   verifies false (Win32 API failure with `hwnd == 0`) instead of a missing-member exception.
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

### Nekopara Vol. 1

- Located at `E:\kk4s-pc\games\NekoPara Vol.1`; this is Kirikiri Z/TJS2/KAG-compatible (PE strings
  include `kirikiriz` and `startup.tjs`; files include XP3 archives, a `.tpm`, and KAGZ plugins). The
  precise upstream engine version is not established.
- First diagnostic used stale `out-desktop/kk4s.exe` (`c0d7705`); it mounted 1201 `data.xp3` entries
  but could not find `startup.tjs`. Rebuilt from the overnight branch (`06f137e`) and repeated with a
  separate `KK4S_ROOT`; it still exits with `Cannot find storage startup.tjs`, before gameplay.
  Switch is NOT TESTED.
- In both runs `compat: saves in ...` points outside the game directory; the existing five save files
  were copied to the isolated test root. SHA-256 manifests (outside Git) cover all 86 files / 3.758 GB
  before and after; **0 files changed**. The game folder is not modified.
- Initial `tools/xp3ls` probe hit `std::length_error`; its v2-header parser was reading the version
  block from the wrong offset. The generic correction now parses all six XP3 v2 archives present
  (data 1201 entries, patch 222, adultsonly 1647, emote 88, emotewin 40, voice 4845).
- The `data.xp3` index contains 1201 `eliF` hashed-name records paired with 1201 `File` records.
  A temporary read-only index inspection confirms that `startup.tjs` exists under the `eliF` name
  mapping. The current engine consumes kk4s/PackinOne `hnfn` records but does not yet map XP3v2
  `eliF`, so the generic engine-side mapping is now the next experiment. No game resources or saves
  are being added to Git.

## Commits and unresolved items

Append new commits, exact validation outcomes, regressions, blockers, and changed files here. Do not
include game files, keys, profiles, saves, build outputs, or patched game content in Git.
