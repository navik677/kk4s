# Handoff: universal KiriKiri compatibility and native PC repacks

Updated 2026-11-01. The CXDEC implementation was merged and pushed to `main`; current PC work adds
generic automatic scheme recovery. Read `docs/cxdec.md` (design/status), `docs/debugging.md`, and
`docs/build-deploy.md` first.

## The goal (user's words, repeated several times)

kk4s must run **any** PC KiriKiri game from its original folder (repack or original), with **no
extraction, patching or conversion** by the user. **ATRI is only a test case**, not the product:
every fix must be generic or gated on positive detection. Do not hardcode ATRI offsets, keys or names.
Do not break existing games: **Fate/FSN with the Russian patch**, Tsukihime Russian, Sharin no Kuni,
and The Makings of a Lady. The user confirms Fate/FSN and the other tested games work on Switch, and
Fate has a full playthrough. Never change `ResolveProjectDir`, the global XP3 mount order, or mount
all `*.xp3` for all games. ATRI is only a test fixture; no ATRI-specific keys, offsets, names or
branches. Keep new changes local for review unless the user explicitly asks to push. The user writes
Ukrainian; keep answers short, outcome first.

## State (ATRI is only a test fixture; universal engine is the goal) - updated 2026-11-01 22:20

ATRI is the PC Steam-repack test fixture only; it is not the product or acceptance target. The
generic CXDEC/Steam path now recovers its scheme automatically on desktop from the embedded PE TPM
(12/12 real-entry validation, with no `profiles/cx` scheme present) and reaches title setup. The
desktop run then encounters general KAGZ gaps (`AlphaMovie` plugin and `TextRenderBase.fontScale`;
the renderer is still a no-op). On Switch the user reports ATRI still does not show its title menu.
The latest Switch log reaches `title:xrated, 0`, then `win32dialog.tjs getNumberdId` indexes
`itemMap` with `void`. Fix reusable KiriKiri Z plugin/dialog semantics, not ATRI-specific behavior.

Desktop automatic CX scheme recovery is implemented in `compat/cxdec/scheme_recover.cpp` with
Capstone, invoked by `BindCxDecoder()` only for positive `hnfn` detection and only after existing
profile schemes fail. Switch still requires a user-data scheme profile; the portable x86/PE analyzer
is a future step. No scheme/key is written to the repo or game folder.

**Regression status:** the user confirms Fate/FSN and the other tested games work well on Switch;
Fate is playable from beginning to end. The XP3-filter/background-thread crash fix is in NRO
`8d4bef4` and the user reports the rest of the suite remains healthy. Keep this full-playthrough
regression mandatory when changing the shared engine or compatibility layer.

What made it pass the old blockers (all in this commit):

- **Steam build detection**: `compat/kk4s_compat.cpp` `DetectSteamBuild()` adds `-steam=yes` when
  `steam_api.dll` + `steam.tjs`/`steam.xp3` are in the game folder (checked on disk, not as storages:
  DLLs are never exposed there). Without it `custom.tjs` never loads `steam.tjs` and `[実績]` is fatal.
- **`-debugwin=no` by default**: KiriKiri Z's startup does
  `debugWindowEnabled = -debugwin != "no" ? inXP3archivePacked : convertMode`; a release exe passes
  `-debugwin=no`. Without it a packed game enables the developer log window (native Win32 dialog) and
  every `kag.errorCmd` dies in it. A developer can still use `KK4S_ARGS=-debugwin=yes`.
- **`krkrsteam.dll` built-in** (`compat/kk4s_plugins.cpp`): a `Steam` object with
  `setAchievement`/`clearAchievement`/`getAchievement` (logged only), `hookScreenshots`,
  `hookBroadcasting`, `isBroadcasting`, `cloudEnabled=false`, `cloudFileCount=0`,
  `getCloudFileInfo`/`deleteCloudFile`/`copyCloudFile`. `steam.tjs` only needs `typeof Steam` to
  exist and `cloudEnabled` to be false; `steam_r18hack.tjs` calls the hook methods.
- **win32dialog stub**: `WIN32Dialog.Header`/`Items` must be **classes with `store()`** (not dictionaries),
  plus `makeTemplate`, static `left/top/width/height`, `setPos`/`setSize`/`maximize`/`restoreMaximize`
  for KAGZ's modeless wrapper. (Static members on the base class are visible on the derived class:
  verified. A class declared inside `if (...) {}` is block-scoped in TJS2 - keep it top level.)
- **`PSBFile` constructor takes the storage**: KAGZ's `storagedata.tjs` uses `new PSBFile(name)` and
  reads `.root` immediately. `ncbind` constructors have a fixed argc, so the class now uses a factory
  that accepts 0 or 1 argument (`engine/source/plugins/psbfile.cpp`).
- **PSB `null` -> TJS void** (not a null object): `world.tjs updateProperty` treats `== void` as
  "no value" and otherwise indexes the value; a null object crashed `restore()` ("Accessing to null
  object"). This one change took the game from `start.ks` to `title.ks`.
- **`textrender.dll` built-in**: KAGZ `system/textrender.tjs` declares `class TextRender extends
  TextRenderBase`; without the class the script fails to load. The stub provides the class, the
  `TextRenderBase` API surface KAGZ scripts call (`fontScale`, `resetFont`, `done`,
  `getCharacters`, `getLinkCharacters`, `getLinkNames`, `getKeyWait`, `onGetTextHeight`,
  `calcLineOffset`, `calcShowCount`, `resetStyle`, `setStyle`, and the `renderLeft/Top/Right/Bottom`,
  `renderCount/Lines/Text/Delay/Over`, `kinsoku_max` results) with no-op drawing (text/effects stay
  invisible until the real renderer is ported).

Next blocks after the title (in order seen so far): the real text renderer (currently no-op), then the
scenario/`layerEx*`/`AlphaMovie` plugins as they are used. `k2compat.tjs makeDummyProperty` throws
"Invalid object context" twice at startup (game-side, non-fatal; same on `main`).

- Last desktop FSN regression: Russian title/menu, `project dir: .../data.xp3>`, 0 script exceptions.
- Latest NRO on the SD: `8d4bef4`, SHA-256
  `4101BB85B3B2AE8E98CAE0D0350B873C281DA34B35EEB277E7FDBE914D02997F`.

## Startup and Steam chain (resolved)

The real `startup.tjs` is the standard KiriKiri Z bootstrap (`kirikiriz`, `-debugwin`,
`inXP3archivePacked`, `convertMode`, `debugWindowEnabled`, then `system/Initialize.tjs`). The old
"XOR 0x61 / obfuscated bytecode" conclusion came from the abandoned magic-XOR attempt. Current ATRI
logs show the real startup runs; `StartupScriptShim()` checks `AnyCxArchiveBound()` before the Cx
archive is bound, so it is not selected. Do not spend time on the shim unless a different protected
title proves it is needed.

Steam support is implemented generically: detect `steam_api.dll` plus `steam.tjs`/`steam.xp3`, add
`-steam=yes`, disable the release debug window by default, and provide the `krkrsteam.dll` Steam
object (achievements logged, cloud disabled). This lets the game-side `steam.tjs` intercept
`kag.errorCmd` for `[実績]`. It is not the remaining PC repack blocker.

## Current PC-repack compatibility blockers (general KAGZ APIs/plugins)

Latest desktop no-profile ATRI test: auto-CX scheme recovery succeeds, then the game hits missing
generic KAGZ components: `AlphaMovie` is not registered and `langrender.tjs` assigns `fontScale`,
which is absent from the current `TextRenderBase` no-op. `AlphaMovie.dll` is a plugin replacement
candidate; inspect its wrapper contract and implement reusable behavior, not ATRI assets or story
logic. The `fontScale` property belongs in the shared text-render compatibility interface. The same
renderer/plugin APIs should be tested with additional KAGZ titles before claiming broad support.

## Tools and methods that worked (use them)

- Build desktop (PowerShell): `$env:PATH="C:\msys64\ucrt64\bin;C:\msys64\usr\bin;"+$env:PATH;
  cmake --build build-desktop -j 4`. Run long things with `run_in_background` and wait for the
  notification (a foreground `sleep` is blocked; a 2-minute foreground limit kills long builds).
- Build Switch NRO: `/c/msys64/usr/bin/bash.exe -lc 'cd /c/Users/navik/gits/kk4s && SKIP_DEPS=1 bash
  tools/build-switch.sh'` -> `out/kk4s.nro`. Upload: `curl.exe -T out/kk4s.nro
  ftp://192.168.1.135:5000/switch/kk4s/kk4s.nro` (console must be awake; verify sha256 by
  downloading back; the user asked for each upload explicitly, ask first). Logs:
  `ftp://192.168.1.135:5000/switch/kk4s/logs/` (game must be closed).
- Run ATRI on PC: `KK4S_ROOT=<empty temp data root>`, argument `"E:\ATRI -My Dear Moments-"`
  (quote the path). To test desktop auto-scheme recovery, ensure `<root>/profiles/cx/` contains no
  scheme. The runtime should log `cxdec: recovered scheme auto:... (12/12 samples verified)`. Any
  old `atri.cxscheme`/`.cb` under `E:\kk4s-pc` is user data; never commit it.
  FSN needs `E:\kk4s-pc\profiles\Fate-stay_night\xp3filter.tjs`; game at `E:\kk4s-pc\games\Fate-stay_night`.
- Logs: `E:\kk4s-pc\logs\kk4s-*.log`. Every script exception is now logged as `(exception) <message>`.
- **Engine disassembly of any bytecode script**: `KK4S_DUMP_BYTECODE=<part of the name>` (e.g.
  `system.tjs`, `psdlayer.tjs`, `kagenvironment.tjs`); `tools/tjsdis.py` is unreliable for function bodies.
- **Inspect a running game**: `KK4S_INPUT_SCRIPT=<file>` with lines `25000 eval <TJS expression>`
  (result in the log) and `+3000 quit`. Only ticks after startup, so errors during the first second
  cannot be caught this way; for those use `KK4S_KAG_TRACE=1` (hooks `kag.errorCmd/errorLine`, logs
  their arguments) and `KK4S_ARGS="-debug=yes"` (keeps the KAG tag trace alive; ATRI's `first.ks`
  otherwise resets the debug level).
- Unscramble KAG text (`.ks`, some `.tjs`): files start `FE FE 01 FF FE`; then UTF-16LE with each code
  unit `((c & 0xAAAA) >> 1) | ((c & 0x5555) << 1)`. Plain extracted game files are in `E:\atri-play`
  (from the old extraction; has the **shim** `startup.tjs`, not the original).
- Extract any entry with the Python reference: `tools/cxdec_ref.py` (`load_scheme`, `.decrypt(adlr,
  0, buf, 0, n)`) plus an index parser (`%TEMP%\atri_keyruns.py`); adler32 of the result must equal
  `adlr`. Use `python -X utf8` for Japanese output; `python -I` ignores env vars/cwd (needed for
  untrusted data) but then `PYTHONIOENCODING` does not apply.
- Scratch test games: a folder with only a `startup.tjs` runs as a game (`%TEMP%\claude\psbtest`).
- Tests: `tests/cxdec` (synthetic vectors + `--archive` mode), `tests/psb` (`run_tests.py`). Both
  built with `g++ -std=c++17 -O2 -I compat ... -lz` (see the headers of the test files).
- Regression check for FSN: run it on the new build and on a `main` worktree
  (`git worktree add --detach <dir> 4d767de`, build in `<dir>/build`); normalize logs (strip time,
  `ms`, pointers, sound/audio/perf lines, `decrypted` lines) and diff. They were identical.
- Don't use `computer-use` for kk4s windows (not allowlisted). Screenshots of the game window:
  PowerShell `CopyFromScreen` of the window rect; **move the window to 0,0 first** (`MoveWindow`),
  otherwise other windows cover it.

## What the last sessions changed in the engine (all generic)

Commits `44a88bd`..`07bc9c1` on this branch: portable CXDEC (`compat/cxdec`), scheme extractor
(`tools/cx_scheme_extract.py`), hnfn name table + Cx binding in `XP3Archive` (only for archives with
`hnfn` records), PSB reader (`compat/psb`) and `psbfile.dll` (`engine/source/plugins/psbfile.cpp`:
`PSBFile`, `psb://` media, cache by storage name), custom storage media names no longer turned into file
paths (`TVPIsCustomStorageMediaName`), temp dir `<data>/tmp` (`kk4s::TempDir`), `win32dialog.dll`
stub (real Win32 constants), `Scripts.foreach/getObjectKeys/getObjectCount/clone/equalStruct`
(`compat/kk4s_scripts.cpp`; `foreach` calls `func(name, value, extra...)` with the caller's `this` and
returns the first non-void callback value — the engine's `Dictionary.forEach` passes `(value, name)`),
`Scripts.isNullContext`/`getObjectContext` (the `tTJSVariantClosure.ObjThis` of a function, used by
KAG's `Action.tjs` completion callbacks),
`[emb escape=false]` and `taglist` for games that link `KAGParserEx.dll` (`KAGParser.cpp`, flag
`TVPKAGParserExMode` set from the compat plugin table), exception messages in the log, debugging aids
(`KK4S_DUMP_BYTECODE`, `KK4S_KAG_TRACE`, `KK4S_ARGS`). The desktop runtime now also contains
`compat/cxdec/scheme_recover.cpp` for data-validated automatic PC scheme recovery.

## Other open items

- `main/imagedressmap.txt` in `data.xp3`: its zlib segment does not inflate (all other entries verify).
- Desktop runtime scheme recovery is automatic for supported PE32/TPM shapes and validates on archive
  adlr samples. Switch still needs a supplied profile scheme; port the generic x86/PE analyzer there.
- The `win32dialog.dll` base classes/constants exist, but the wrapper/log-window contract is incomplete:
  ATRI Switch logs `getNumberdId` indexing `itemMap` with `void`. Fix the general KAGZ dialog semantics,
  not a game-specific callback.
- KAGZ PC support still lacks a real `AlphaMovie.dll` (AJPM/`.amv` video), `Scripts.loadDataPack`/
  `saveDataPack` (the `.pbd` `UKR.5r1` data packs), and the drawing half of `textrender.dll`; the
  `TextRenderBase` API surface now exists but draws nothing. Implement from the plugin contracts and
  verify another KAGZ game.
- Memory: `PSBFile.root` is built eagerly (largest ATRI `.scn` ~126k nodes); fine so far, untested on
  Switch with many files loaded.
- `E:\atri-cx-probe` (hard-link mirror of ATRI used to dump memory) can be deleted.
- User-confirmed regression: Fate/FSN and the other tested titles work on Switch; Fate is playable
  from beginning to end. Before future shared changes, retain that complete-playthrough check plus
  Russian text/fonts, sprites/backgrounds, music, transitions, save/load, and the other compatibility
  titles. Follow `docs/HANDOFF.md`'s FSN precedence rule.

## FSN crash on Switch (crash report received after 22:00 run)

The new Switch log is `kk4s-20261101-220049.log` (build `ba29f9c`), followed by Atmosphere report
`01793566882_03db116921e60000.log` (2168-0001, 2026-11-01 22:01:22). The user is right: **kk4s
crashed**, it did not just leave the game stuck. The FSN log stops at 22:01:21 while loading
`AH01a.tlg`; the crash report was generated one second later.

### Crash report diagnosis (symbolicated with the matching `out/kk4s.elf`)

The crashed thread is the asynchronous graphics loader. Its stack is:

```text
TJS::tTJSObjectProxy::AddRef()
TJS::tTJSInterCodeContext::ExecuteAsFunction(...)
TJS::tTJSInterCodeContext::FuncCall(...)
std::atomic<unsigned int>::fetch_add / operator++
ScriptExtractionFilter                         compat/kk4s_compat.cpp:224
tTVPXP3ArchiveStream::Read                     engine/source/core/base/XP3Archive.cpp:1171
tTJSBinaryStream::ReadBuffer
TVPLoadTLG5 / TVPInternalLoadTLG / TVPLoadTLG
TVPLoadGraphicRouter
tTVPStreamHolder::~tTVPStreamHolder
tTVPAsyncImageLoader::LoadImageFromCommand      engine/source/core/visual/GraphicsLoadThread.cpp:292
tTVPAsyncImageLoader::LoadingThread
```

So the failure is a **TJS2 XP3 extraction filter being called from the image worker thread** while
loading an FSN TLG. TJS2 is not thread-safe. The log itself also says
`compat: xp3 filter script called on a background thread (unsafe)`. The bad `AddRef`/proxy dispatch
is the crash mechanism; it is not evidence that the TJS global was invalidated, and the new
`EventIntf` catch-and-continue change did not address it. The user's report that kk4s crashed is
confirmed by the Atmosphere `Instruction Abort` report.

### XP3-filter crash fix (hardware-confirmed by user, 2026-11-01)

When `ScriptFilterActive()` is true, `tTVPAsyncImageLoader::LoadRequest` loads graphics synchronously
on the main thread using the existing `TVPLoadGraphic` path. `TVPTouchImages` likewise skips its
asynchronous preload branch and uses its synchronous loop. Unfiltered games keep the existing async
path. This follows the existing XP3 rule that eagerly decrypts filtered files on the main thread and
removes the observed TLG-worker call into TJS. Desktop FSN regression test with Switch save
`data316.bmp`: loads and advances, **0 script exceptions**, and no
`xp3 filter script called on a background thread` warning; filtered TLG/OGG reads are logged as
main-thread decrypts. Switch NRO `8d4bef4` (SHA-256
`4101BB85B3B2AE8E98CAE0D0350B873C281DA34B35EEB277E7FDBE914D02997F`) was uploaded to the SD.
The user confirms Fate/FSN and other games now run well, with Fate playable from beginning to end;
this confirms the filter-threading fix for the tested suite. Continue regression testing any change
to XP3 reading or graphics loading.

The earlier 21:00 interpretation of the `202113` log was too confident. That older run did show an
exception at `kaglayer.tjs(87) onTransitionCompleted` after FlowTracker's `*skip_to_next`, but the
new crash report is a separate, symbolicated failure in the XP3-filter/image-worker path. Do not
conflate them or claim that suppressing an idle exception fixes this crash.

Historical log details from the older run:

- The user had just used **FlowTrackerPlugin's "skip to next scene"** link (`*skip_to_next`,
  `@playstop time=200`, `@sestop`, `@cm`) and the game jumped from `...-11.ks` through `???????.ks`
  into `...-14.ks`. The crash comes 3 s into the new scene, right after the first `@fadein A23e`.
- It failed in `onTransitionCompleted` after the scene jump. That older log has no native crash
  report attached, so it cannot establish the cause of the later 22:01:22 crash.
- PC reproduction with the same SD save did not reach it: the bookmark (`data316.bmp`, identical
  hash on the Switch and the PC) loads and the PC build plays a different branch (FlowTracker links
  and timings differ). It is timing/scene-jump dependent, so far Switch only.

The invalid-register diagnostic remains available for future script exceptions, but it is not the
diagnosis for this crash. Keep investigating the older transition exception separately if it recurs
after the unsafe filter call is removed.
