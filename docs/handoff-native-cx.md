# Handoff: native Cx/PackinOne support (ATRI as the test case)

Written 2026-11-01 for the next model. Branch **`wip/packinone-native`** (not on `main`, nothing pushed).
Read `docs/cxdec.md` (design, status), `docs/debugging.md` (debugging aids), `docs/build-deploy.md`
(exact build/FTP commands for this dev PC) first. This file is the "what to do next".

## The goal (user's words, repeated several times)

kk4s must run **any** PC KiriKiri game from its original folder (repack or original), with **no
extraction, patching or conversion** by the user. **ATRI is only a test case**, not the product:
every fix must be generic or gated on positive detection. Do not hardcode ATRI offsets, keys or names.
Do not break existing games: **FSN with the Russian patch** (`rufix.xp3` + `rupatch.xp3`) and
Tsukihime Russian must keep working; compare against `main` (`4d767de`/`0576d4e`). Never change
`ResolveProjectDir`, the global XP3 mount order, or mount all `*.xp3` for all games. `main` is not
to be modified. The user writes Ukrainian; keep answers short, outcome first.

## State (desktop build, original ATRI folder)

- Cx (CXDEC) archives decrypt natively; scheme verified per archive (see `docs/cxdec.md`).
- The game **renders its first screens** (logo, WARNING page with `.pimg` image and text).
- It then dies on `[実績 name=開始]`: that macro calls `kag.errorCmd("実績:開始")` on purpose; **`steam.tjs`
  (with `krkrsteam.dll`) overrides `kag.errorCmd`** to turn these calls into achievements. Without it
  the call is a fatal error ("致命的なエラー", then the game tries to open a Windows log window).
- Last verified: FSN desktop log identical to `main` after all changes (0 exceptions).
- **Switch**: the NRO on the SD card is an *older* build (`174792f`, 19:28): it has the Cx decoder,
  win32dialog, Scripts.foreach etc., but NOT psbfile/media fixes/KAGParserEx/equalStruct. A local Switch
  build of an earlier state compiled fine. The user tested ATRI on Switch: decrypts, reaches
  `title.ks`, black screen (same as desktop at that time). FSN/Tsukihime on Switch with the newest code
  are **not tested**.

## Key discovery of the last session (do this first)

The **real `startup.tjs` is NOT obfuscated and NOT Steam related**. It is the standard KiriKiri Z start
(`kirikiriz`, `-debugwin`, `inXP3archivePacked`, `convertMode`, `debugWindowEnabled`,
`Scripts.exec("@set(PACKED=1)@set(DEBUG=..)")`, `Scripts.execStorage("system/Initialize.tjs")`), 1300
bytes. The old "XOR 0x61 / obfuscated bytecode" conclusion came from the wrong magic-XOR decryption.
With correct CXDEC the engine loads it fine. Verified: with env `KK4S_NO_SHIM=1` ATRI starts through
its own `startup.tjs` and behaves exactly like with the shim (same flow, same achievement error).

**Therefore the KiriKiri Z startup shim is unnecessary: remove it.**
- `compat/kk4s_compat.{h,cpp}`: delete `StartupScriptShim()` (and the uncommitted `KK4S_NO_SHIM` line).
- `engine/source/core/base/ScriptMgnIntf.cpp` (`TVPExecuteStartupScript`): restore the plain
  `TVPExecuteStorage(TVPStartupScriptName)` (see `git show 4d767de:...` for the original).
- `kk4s::compat::AnyCxArchiveBound()` becomes unused (keep or remove; it was only the shim's gate).
- The shim also overrode `System.inform` (blocking message box) and `System.terminate`. Check that
  without it a game calling `System.inform` does not block on PC/Switch (the ATRI run did not hang).
- Update `docs/repacks.md`, `docs/cxdec.md`, `docs/packinone-native.md` (that one only exists on `main`).

## Next blocker: the Steam chain (generic design)

How `steam.tjs` gets run (found in `main/custom.tjs`, readable via the unscrambled text):

```
switch (System.getArgument("-steam")) {
case "yes":
	if (Storages.isExistentStorage('steam.tjs')) KAGLoadScript('steam.tjs');
	else { System.inform("Internal error: steam.tjs not found."); System.exit(1); }
```

So the **Steam build starts the engine with `-steam=yes`** (the Windows EXE/PackinOne adds it; kk4s
does not). `Config.tjs` itself treats a folder as a Steam build when
`inXP3archivePacked && (isExistentStorage('steam.tjs') || isExistentStorage(exePath+"steam_api.dll"))`.
Suggested generic rule: when `steam.tjs` exists in the mounted storage **and** `steam_api.dll` is in the
game folder, add `-steam=yes` to the program arguments (there is `KK4S_ARGS="-steam=yes"` already for
testing: `engine/source/core/base/win32/SysInitImpl.cpp`, `PushAllCommandlineArguments`). Keep the rule
in the compat layer, logged, and gated on both markers.

`steam.tjs` then does `Plugins.link("krkrsteam.dll")` and needs a **`Steam` object**. kk4s has no such
plugin: add a **`krkrsteam.dll` compat stub** in `compat/kk4s_plugins.cpp` (table `kPlugins`, see
`LinkKAGParserExDll`/`LinkUtilDll` for the pattern; remember anything in that file lives in
`namespace kk4s::compat`). Methods seen in `steam.tjs` (from `KK4S_DUMP_BYTECODE=steam.tjs`, dump saved
by the previous run at `%TEMP%\claude\steam.dis` if it still exists): `setAchievement`,
`clearAchievement`, `loadAchievementList`?, cloud: `getCloudFileInfo`, `cloudFileCount`,
`deleteCloudFile`, `copyCloudFile`, `cloudEnabled`; also `SteamPlugin`, `SteamDrawDevice.dll`
(`CanLoadPlugin`; draw device plugin `plugin/SteamDrawDevice.dll`, `DualDrawDevice_` fallback).
**Read `steam.tjs` properly before stubbing** (re-dump it with `KK4S_ARGS=-steam=yes
KK4S_DUMP_BYTECODE=steam.tjs`, no shim). A stub should report "Steam not running / not initialized"
and make every call a no-op returning a sane value; achievements can be logged only. Then check the
override of `kag.errorCmd` really catches `実績:...`.

Other plugins the game links and kk4s ignores (see log lines `compat: plugin not available, ignored`):
`PackinOne.dll`, `layerExImage/Raster/Draw/BtoA`, `textrender`, `windowEx`, `k2compat`, `AlphaMovie`,
`scriptsEx`, `wuopus`, `extNagano`… Implement only what a run actually needs; look for the first
exception each time. AlphaMovie/krmovie matter for the opening movie.

## Tools and methods that worked (use them)

- Build desktop (PowerShell): `$env:PATH="C:\msys64\ucrt64\bin;C:\msys64\usr\bin;"+$env:PATH;
  cmake --build build-desktop -j 12`. Run long things with `run_in_background` and wait for the
  notification (a foreground `sleep` is blocked; a 2-minute foreground limit kills long builds).
- Build Switch NRO: `/c/msys64/usr/bin/bash.exe -lc 'cd /c/Users/navik/gits/kk4s && SKIP_DEPS=1 bash
  tools/build-switch.sh'` -> `out/kk4s.nro`. Upload: `curl.exe -T out/kk4s.nro
  ftp://192.168.1.135:5000/switch/kk4s/kk4s.nro` (console must be awake; verify sha256 by
  downloading back; the user asked for each upload explicitly, ask first). Logs:
  `ftp://192.168.1.135:5000/switch/kk4s/logs/` (game must be closed).
- Run ATRI on PC: `KK4S_ROOT=E:\kk4s-pc`, argument `"E:\ATRI -My Dear Moments-"` (quote the path).
  Scheme for ATRI is in `E:\kk4s-pc\profiles\cx\atri.cxscheme` + `atri.cb` (user data, never commit).
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
`[emb escape=false]` and `taglist` for games that link `KAGParserEx.dll` (`KAGParser.cpp`, flag
`TVPKAGParserExMode` set from the compat plugin table), exception messages in the log, debugging aids
(`KK4S_DUMP_BYTECODE`, `KK4S_KAG_TRACE`, `KK4S_ARGS`).
Uncommitted: only the `KK4S_NO_SHIM` env line in `compat/kk4s_compat.cpp` (to be removed with the shim).

## Other open items

- `main/imagedressmap.txt` in `data.xp3`: its zlib segment does not inflate (all other entries verify).
- The extractor `tools/cx_scheme_extract.py` is a PC Python tool (needs `capstone`, `pefile`). For
  "folder -> play" on Switch without a manual step it must be ported into kk4s (a small x86 decoder for the
  generator's switch tables) or the scheme file must be produced once on a PC. This is the remaining
  manual step; see "When automatic recovery is not possible" in `docs/cxdec.md`.
- `win32dialog.dll` stub has no `WIN32Dialog.Header`/`Items` *classes* (only dictionaries), so the game's
  Windows log window path (`initLogPad`) still throws "Not a function"; harmless unless an error occurs.
- Memory: `PSBFile.root` is built eagerly (largest ATRI `.scn` ~126k nodes); fine so far, untested on
  Switch with many files loaded.
- `E:\atri-cx-probe` (hard-link mirror of ATRI used to dump memory) can be deleted.
- Before merging anything to `main`: re-run the regression list the user gave (FSN Russian text and
  fonts, sprites/backgrounds, music, transitions, save/load, other translated games), preferably on the
  Switch too. Follow the existing rule in `docs/HANDOFF.md` about FSN precedence.

## FSN hangs on Switch (reported after the 20:20 build, unresolved)

Log `kk4s-20261101-202113.log` (Switch, FSN, scenario `セイバールート十一日目-14.ks`): the game plays
normally, then `@fadein file=A23e time=200 rule=円形(中から外へ) vague=64` is followed by
`(exception) The object is already invalidated` in `kaglayer.tjs(87) onTransitionCompleted`, then
`Script exception raised`. The log then stops (no `shutdown`), so the game is stuck, not crashed.

Suspicion (not verified): a transition's completion callback fires for a layer that was already
invalidated (cleared or removed by the scene change while the fade was running). The engine's
transition/event path delivers the callback after `Invalidate` and the TJS exception escapes into the
event loop, which does not return. Check in this order:
1. Does the same happen on `main` (4d767de) for the same scene? If not, the cause is in our changes
   (candidates: `Scripts.foreach` returning values, the `[emb]` escape change, `taglist`), so bisect
   those commits on the PC with `KK4S_KAG_TRACE`.
2. Which layer is `onTransitionCompleted` invoked on, and what cleared it (`clearlayers`/`@allimage`
   right before the fade). Dump with `KK4S_DUMP_BYTECODE=kaglayer.tjs`.
3. The C++ side: the transition object (`tTVPTransition`/`LayerIntf` transition completion) should
   check the owner is still valid before calling back.
Do not "fix" it by swallowing the exception in TJS without knowing the cause.
