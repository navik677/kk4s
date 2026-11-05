# Handoff — read this first in a new session

Last update: 2026-11-05. Goal: **a universal Kirikiri runtime/compatibility layer**: any KAG/TJS2
game folder (plain or repacked) runs unmodified on Switch and PC. ATRI is one PC-repack test fixture,
not the target. No per-game fixes, game files, keys or conversion; compatibility belongs in the engine,
`compat/` or kk4s' own files. Full requirements: `docs/original-prompt.md`; milestones:
`docs/roadmap.md`.

## Where we are (2026-11-05)

**Branches:** everything is merged into `main` (fast-forward, `c6649c7` at the time of writing, 46 commits
after `49dfe56`): `cloud-session` (session 3), `research/emote-psb`, `research/emote-crossgame`,
`feature/universal-emote-runtime`, `work/game-compat-patchtjs`. Work from here starts from `main`. The older
"on `cloud-session`" remarks below describe where a change was made, not an unmerged state. Hardware: the
Switch NRO of `main` built locally (29,259,173 bytes) but nothing after `49dfe56` ran on a console yet.

### Session 4 (PC, Windows) — Kirikiroid2 packs and Russian games; E-mote runtime started

Branch `work/game-compat-patchtjs` (based on `feature/universal-emote-runtime`, which contains the
`research/emote-crossgame` engine fixes). Goal of the session: the games the user dropped into
the PC `games/` folder start and play; Witch on the Holy Night shows its **Russian** translation. Only the engine
was changed; no game file was touched (the games only wrote their own `savedata`).

| Change | Why (evidence) |
|---|---|
| `patch.tjs` of the game folder runs before `startup.tjs` (`compat/kk4s_compat.cpp`, before the engine-native members) | Kirikiroid2 packs use it for `-langid`, `class Steam`, `Layer.*` stubs. Karigurashi exited with "Cannot load languageID!", 11 eyes and Witch died at startup |
| `fstat.dll` always replaces `Storages.dirlist/dirlistEx/dirtree` (`compat/kk4s_plugins.cpp`) | Witch's `patch.tjs` stubs `dirlist` as `return []`; the game lists `savedata/patches` with it, so the Russian `patch155.xp3` (scenarios in the archive root, Japanese ones in `data.xp3>scenario/`) was never mounted. Now `patch[0..2]` mount and the text is Russian |
| `layerExParticle.dll` stub (`compat/kk4s_layerfx.cpp`) | Witch's patched scripts call `super.uninitParticle(...)`; window creation failed (`Member "kag" does not exist`) |
| `WIN32Dialog.ICC_*` constants (`engine/source/plugins/win32dialog.cpp`) | 11 eyes `rebind.tjs`: `ICC_BAR_CLASSES` missing |
| `Steam.restartAppIfNecessary` (false) / `initializeSteamAPI` (true), also forced onto a pack's stub class | Karigurashi `override.tjs` exits with `System.exit(1)` otherwise |
| An `*.xp3` that is not an XP3 is skipped with a warning (`StorageIntf.cpp`) | Karigurashi's `adult.xp3` is a ZIP; it aborted every storage lookup. The game now runs without that patch (ZIP support is not implemented) |
| **Windows manifest `activeCodePage=UTF-8`** (`platform/desktop/kk4s.manifest`, `kk4s.rc`, CMake) | The narrow CRT used ANSI cp 1252, so files with Japanese names were "missing": Yosuga checks `ヨスガノソラ.cf` next to its exe and quit with "正常に起動できませんでした". Affects every Windows game with non-ASCII file names |
| `Motion.Player.allplaying` (`compat/kk4s_motion.cpp`) | NekoPara reached its first character scene and died in `motionaffinesourcelayer.tjs` |

Seen on the PC with `dist/kk4s-pc/kk4s.exe` (exe + MSYS2 DLLs; **a bare `build-desktop/kk4s.exe` needs
`C:\msys64\ucrt64\bin` in PATH**), 25 s per game, window title checked: Fate, 11 eyes, Karigurashi Renai,
Witch (RU), Yosuga, Gyaru Express, Niji no Kanata ni, Chronobox, redwitch_rus, Tsundere na Imouto run.
Still failing: **NekoPara Vol.1** (exits after ~9 s: `Image Load Error / Invalid image` in
`imageaffinesourcelayer.tjs loadImages`, with and without this session's changes; the E-mote scenes are
not drawn at all), **Steins;Gate (RU beta)** (exit 0 right after start, not looked at), **UsotsukiGenius**
(ran once, then exit `-1073741571` = stack overflow, flaky, not looked at). Not run: the synthetic test
suite after these edits (Windows has no `python3`/bash setup here), Linux, Switch. Games that rely on a
pack's stub of a plugin kk4s links itself are now decided by kk4s' built-in (fstat, krkrsteam); other
Kirikiroid2 stubs (`Layer.*` of unsupported plugins) are still honoured as the pack defines them.

### Switch feedback and follow-ups (2026-11-05, late)

First Switch run of the session-4 NRO (logs read over FTP, sdmc:/switch/kk4s/logs/):
- **Yosuga no Sora** did not start: the SD copy lacked `ヨスガノソラ.cf` / `ヨスガノソラ.exe` (files with Japanese
  names did not survive the copy); the game checks that `.cf` and quits with "正常に起動できませんでした". The
  `.cf` (958 bytes) was uploaded over FTP afterwards (the Switch FTP server accepts UTF-8 names but does not list
  them, `SIZE` shows the file). Copy such files with a card reader / MTP, not FTP, when in doubt.
- **11 eyes** was not listed: it sat in `games/11 eyes/11 eyes/`. The launcher and `SetGameDir` now accept a
  wrapper folder whose single sub-folder (up to two levels) is the game (`platform/common/kk4s_common.cpp`).
- **Screen mode for 4:3 games on 16:9:** `Settings::ScreenMode()` = original with bars (default) / stretch /
  zoom to fill (crop). Stored as `keep_aspect` + new `zoom_fill` in `settings.ini`; row "Screen" in the
  launcher settings and in the in-game menu (`kk4sGameRect` in `engine/source/core/sdl2/stubs.cpp`). Touch /
  mouse mapping goes through the same rect. Seen on the PC (Fate logo fills 1280x720 in zoom mode); not seen on
  the Switch.

### E-mote (`compat/emote/`, merged into `main`)

Universal E-mote runtime started in `compat/emote/` (design, evidence, limits: `docs/universal_emote_runtime.md`).
Pure C++17 + zlib, built into CMake (desktop and Switch objects compile) but **not yet used by
`Motion.*`**. Done: PSB core on `compat/psb`, cipher profiles chosen by evidence (the NekoPara key exists only
in `profiles/nekopara_vol1.cpp`), value model, character model + adapter for E-mote content 3.03
(`spec krkr|win`), animation runtime (variables/tweens/timelines), active-frame scene, renderer contract.
Real data: all 157 NekoPara PSB load end to end, 0 unresolved references, 68k synthetic checks pass.
**Not done:** drawing anything. Needs researched geometry (`mesh.bp`, `transformOrder`, masks, blend
codes), the `RL` icon codec and `DXT5` atlases (the `dxlow_` files). Next: (1) fix NekoPara's
`Invalid image` stop above (probably an image reference of an E-mote layer), (2) wire `LoadCharacter` into
`Motion.EmotePlayer` (timelines, variables), (3) CPU backend into a Layer bitmap, static pose first.

### Branch `cloud-session` — universal-runtime sprint (2026-11-05)

**STOP POINT 3 (2026-11-05, end of session 3 on the local Fedora box).** `main` = `49dfe56` (the sprint up
to the layerExDraw WIP, merged and seen on the Switch: games run, Fate/FSN and Tsukihime show 4:3 with
bars). `cloud-session` is ahead of it with session 3 (steps 1-5 below): sanitizer fixes, layerExDraw built,
optional soft mode, the `.tpm` x86 interpreter, sprintf fixes, ripple / BGRA checks. Test state at the
last code commit: **35/35** normal (GCC 16) and **35/35** ASan+UBSan (clang 22, halt on error, no
report); the NRO builds (29,029,797 bytes, not deployed). Nothing of session 3 was run on the Switch or
with a real game. Next, in this order:
1. Hardware check of this branch (FSN incl. the Russian patch, ATRI, Tsukihime, NekoPara), then merge.
2. A real game's `.tpm` through the interpreter: its log names any missing Win32 / tp_stub function
   (`tpm: ... (not provided by kk4s)`); add those to `compat/x86/tpm_plugin.cpp`.
3. Aspect: 16:9 by stretch or an upscaler + a launcher switch original 4:3 / 16:9 (user, 2026-11-05).
4. NekoPara on the Switch (title / new game work on the PC since `7c3f2ea`); E-mote drawing; plugins by real logs.
5. Open question noted in step 1: TLG5's fast path for overlapping LZSS matches (compare with krkrz).


**Goal of the sprint** (user brief): kk4s must run *any* Kirikiri2/KAG/TJS2 game folder unmodified —
commercial, doujin, ancient, KAGZ, odd plugins and archives. No per-title code; implement the engine
semantics, plugin contracts and formats whole classes of games depend on; prefer what the most games
need. Work lives on `cloud-session` (`main` untouched), committed in small steps; this section is the
log. Nothing here was run on a Switch or against a real game (none is available in the cloud
session); "tested" below means the synthetic suite on the Linux desktop build unless stated.

| Commit | What | Verified by |
|---|---|---|
| `448acb5` | layerExBtoA / layerExImage / layerExRaster natively (`compat/kk4s_layerex.cpp`), real wamsoft contracts; PackinOne links them. The old shim's guessed `clipAlphaRect(x, y, w, h)` silently did nothing for ATRI's 8-argument calls | `tests/layerex` |
| `788689e` | Engine pixel order: krkrs layers are **RGBA bytes**. `Layer.getMainPixel` returned R/B swapped; AlphaMovie frames and `drawLine` were R/B swapped | `tests/layerex` colour round-trip |
| `230b301` | docs for the above | — |
| `6999bfb` | fstat.dll complete: `dirlist`/`dirlistEx`/`dirtree`, `getTime`/`setTime`, FILETIME helpers, attributes, `getMD5HashString`, `TemporaryFiles`… | `tests/fstat` (also checks the game folder is untouched) |
| `838fe26` | layerExSave.dll (PNG/TLG5 thumbnails, crop/diff/ooze helpers, async `Window.startSaveLayerImage`); engine **TLG5/TLG6 writers swapped R/B** — fixed (`tTVPTLGSourceLines`) | `tests/layersave` (PNG decoded in Python, TLG5/TLG6/BMP round trips) |
| `348300f` | Engine: GCC 13.3 + UBSan miscompiled `fmtint()` in `tjsSnprintf.cpp` → every ASan/UBSan run crashed at its first log line (**this is why CI on `main` is red since `7a77878`**). Plus three UB fixes ASan found: 8bpp palette `delete` vs `new[]` (every province image), `rand() << 16` overflow, `tTJSString::GetLen` through NULL | ASan+UBSan build: 16/16, no sanitizer reports |
| `38ed254` | fstat made portable (newlib/MinGW lack `st_mtim`, `utimes`, `truncate`, `access`) | **Switch NRO builds** (devkitA64 docker) |
| `bc27470` | json.dll (`Scripts.evalJSON`/`evalJSONStorage`/`saveJSON`/`toJSONString`, the DLL's lenient reader) and the rest of scriptsEx.dll | `tests/json` |
| `b69dc28` | docs (this log, status, roadmap) | — |
| `9d974ca` | Launcher/project dir recognise every Kirikiri layout: `data/`-only folders, `content-data/`, `data.exe`, XP3 appended to an exe (PE overlay); `kk4s --list-games` | `tests/xp3/layout_smoke.py`; `xp3-precedence` (FSN rule) still passes |
| `f3923a0` | windowEx.dll complete: `System.getMonitorInfo`/`getDisplayMonitors`/`getSystemMetrics`/`readEnvValue`/`expandEnvString`/cursor…, Window rects/IME/hit tests, MenuItem/Pad members | `tests/windowex` |
| `2bcb5cb` | **Engine: a closed window kept receiving input** (use-after-free after every `invalidate` of a dialog window such as KAG's `askYesNo`; on the Switch the game looked frozen after the first yes/no prompt) | `tests/kag3` on normal + ASan builds |
| `ec5b540` | Engine UB: `ObjectList` `TJS_free` of a `new[]` array, uninitialised `KAGParser` state copied by KAG snapshots, misaligned TJS hash-table values | ASan/UBSan clean on `tests/kag3` |
| `014a756` | **`tests/kag3`: the original KAG3 system scripts** (vendored, unmodified) play a Shift_JIS scenario: text, images, three transition types, move, quake, fonts/ruby, macros, if, call/return, SE/BGM, emb, bookmark save, link choice, `askYesNo` | 19/19 on normal and ASan builds |

| `cb2f969` | docs for the above | — |
| `1b0aa6c` | `tools/native-api.py`: lists / diffs the native members of built-in classes between source trees | used for `docs/api-coverage.md` |
| `34ce2f3` | Kirikiri Z / Kirikiri 2 native members for every game: the fstat + windowEx sets (native in krkrz, so KAGZ calls them without `Plugins.link`), `System.inputString` (Switch keyboard), `Debug.visible`, language properties | `tests/natives` |
| `0dc90b1` | **Keyless `.tpm` encryption discovery** for plain-named archives (`compat/kk4s_xp3crypt.cpp`): 12 GARbro families + fitted "one XOR byte per entry = f(adlr)" formulas, bound only when adlr verifies | `tests/xp3/crypt_smoke.py` (18 schemes + 2 controls) |
| `806395c` | **Aspect-correct scaling** (letterbox, default on, launcher setting) with touch/mouse mapped through the picture rect; the PC mouse was unscaled before | `tests/aspect`; KAG3 screenshot. **Hardware 2026-11-05:** games run, but Fate/FSN and Tsukihime show 4:3 with bars where they used to fill 16:9 (user wants 16:9) — see Next steps |
| `f484e94` | docs for the API diff, natives, xp3 crypt and aspect commits | — |
| `396b6af` | `tests/kag3`: `[save]` at a save-point label and `[load]` back, `f.*` restored | `tests/kag3` |
| `b6ec74b` | shrinkCopy.dll (`shrinkCopy`, `shrinkCopyFast`) and layerExAreaAverage.dll (`stretchCopyAA`), area-average resampling | `tests/layerex` |
| `82bee15` | **Input follows Kirikiri's Windows rules** (krkrz `TVPWindow.cpp` / `WindowForm.cpp`): `onClick` only for the left button, at the press point, before `onMouseUp` (pad B used to "click" anything reacting to `onClick`); double click → `onDoubleClick`; a wheel notch is **120** (KAG3's history does `delta \ 120`, so it never scrolled); `ssLeft`/`ssRight`/`ssMiddle` in mouse shifts and `System.getKeyState(VK_LBUTTON…)`; `Layer.cursorX/Y` in game coordinates (were window pixels / 0,0 on touch); `setCursorPos` makes a mouse move (KAG3 arrow-key link highlight) | `tests/input` |
| `cd79ed5` | **Pad**: L / R = wheel up / down by default (KAG3 opens its backlog on wheel up, R or Shift+Up — never Page Up, so L did nothing); held buttons repeat like krkr2's pad input (`DInputMgn.cpp`: 500 ms, then 30 ms; wheel 100 ms); the pad's right click / wheel happen at the screen centre until a touch places the cursor (B hit KAG3's "previous page" button at 0,0) | `tests/input`, `tests/kag3/interact_smoke.py` |

| `02dfe4d` | docs for the input commits and the user's plugin-by-class plan | — |
| `b644084` | **extrans.dll as an engine internal plugin** (`engine/source/plugins/extrans/`): krkrz's wave, mosaic, turn, rotatezoom, rotatevanish, rotateswap, ripple instead of cross-fades; scan lines from krkrs' software textures, `bgcolor` RGB → RGBA order, turn table computed (identical to the generated 422 KB file), ripple `(char)` → `(signed char)` (ARM64 `char` is unsigned: out-of-bounds reads) | `tests/extrans` (half-way screenshots; ripple only finishes) |

Test state at `b644084`: `bash tools/run-tests.sh` → **25/25** on the normal desktop build. The
ASan+UBSan build was 24/24 at `cd79ed5` (input and KAG3 tests also with
`UBSAN_OPTIONS=halt_on_error=1`); extrans has **not** been run under ASan yet.

| `d7b1d0e` | `tests/extrans`: ripple's displacement checked on a 16 px checkerboard (+ cross-fade control) | `tests/extrans` |
| `f992aa6` | **TJS real → string like Kirikiri 2**: krkrs' `TJS_snprintf` printed `%g`/`%e` as `%f` (`2.5` → `"2.500000000000000"` in every real-to-string conversion) | `tests/savestruct` |
| `836a911` | saveStruct.dll current wamsoft contract (`sso*`, `Scripts.toStructString`, …; the old copy wrote raw UTF-16 / unterminated strings) | `tests/savestruct` |
| `667cc1b` | varfile.dll writes back to its variable again (Kirikiroid2 port lost it); csvParser read modes | `tests/csvvar` |
| `17d4c2f` | **KAGParserEx.dll is the real extended parser** (multi-line tags, `[pmacro]`, `emb escape=false`, macro `*`, `hasLabel`; only `taglist` was emulated) | `tests/kagparserex` |
| `ebd5f92` | addFont.dll return values (void / 0 / count) over the native `System.addFont` | `tests/csvvar` |
| `a9cdcaa` | **psdfile.dll**: wamsoft psdfile glue + vendored psdparse (MIT), RGBA layer order, packed BMP header for `psd://` | `tests/psdfile` |
| `476353c` | psdparse builds for the Switch (no mmap in libnx) | Switch NRO builds |

Test state at `476353c`: 29/29 on the normal desktop build; the Switch NRO builds. ASan was last run
at `cd79ed5` (24/24) — everything after that (extrans, saveStruct, KAGParserEx, psdfile) has not been
under ASan yet.

Session 3 (2026-11-05, local Fedora box, user's task list). **Step 1: full ASan + UBSan run.** First
run of extrans / saveStruct / KAGParserEx / psdfile and everything else under sanitizers since `cd79ed5`,
built with clang 22 (see Dev setup), with `UBSAN_OPTIONS=halt_on_error=1`:

| Commit | What | Verified by |
|---|---|---|
| `f9554f9` | csvParser `initStorage` deleted an `iTJSTextReadStream` through the interface (no virtual destructor): ASan new-delete-type-mismatch; now `Destruct()`, also on a throw | `tests/csvvar` under ASan |
| `cc6de88` | TLG5 slide decoder / TLG6 bit pool: unaligned `uint16`/`uint32` loads → `memcpy`; TLG6 chroma decode shifted negative values → unsigned shifts (same bytes out) | `tests/layersave` (TLG5/TLG6 round trips) under UBSan |
| `81f8794` | psd:// in-memory BMP: pixels at offset 56 instead of 54 (psdparse writes `uint32_t`) | `tests/psdfile` under UBSan |
| `10006a6` | wheel: sdl2-compat delivers notches only in `preciseY` (y == 0); scripted wheel events were lost on this box | `tests/input` |
| `b88dfbb` | clang build fix (const `GetHint`), `-fno-sanitize=null`: TJS calls methods on a null `tTJSVariantString` (the empty string) by design, `if(!this)` + `-fno-delete-null-pointer-checks` | whole suite |

Test state at `b88dfbb`: **29/29** normal (GCC 16), **29/29** ASan+UBSan (clang 22, halt on error), no
sanitizer report left in the suite's stderr. Not run: leak checking (`detect_leaks=0`, third-party leaks).
Open question noticed on the way, not changed: `TVPTLG5DecompressSlide_c`'s fast path copies a match with
`memcpy`/`memmove` when `mpos + mlen < 4096`, which differs from a byte-by-byte LZSS copy when the match
overlaps the write position (`mpos < r < mpos + mlen`). Our encoder's round trips pass; whether real TLG5
files contain such matches is unchecked (compare with krkrz's `tvpgl.c` before touching it).

**Step 2: layerExDraw.dll is built** (resolves stop point 2 below). The `free(): invalid pointer` was
not ncbind's hook nor `DrawInfo` copies: `LayerExDraw.cpp` declared `std::string kk4s::GameDir()` by
hand, the real one returns `const std::string&`; the return type is not in the mangled name, so it linked
and the caller destroyed a string that was never built (GCC: double free in `ensureFonts`). Under clang
it happened to survive and showed two more bugs: a global `FontInfo` in both FreeType.cpp and the plugin
(ODR: one inline destructor for both, SEGV at exit) and ncbind's `p-1` on a null parameter array.

| Commit | What | Verified by |
|---|---|---|
| `059c781` | FreeType.cpp's `FontInfo` → `tTVPFTFontInfo` (ODR clash with layerExDraw's TJS-bound `FontInfo`) | `tests/layerexdraw` under ASan |
| `c3d6a04` | ncbind `paramsFunctorWithInstance`: no `p-1` when a property get has no parameters | UBSan |
| `7f08c37` | layerExDraw.dll in the build (`CMakeLists.txt`, `PLUTOVG_BUILD_STATIC`), real kk4s headers instead of hand-written declarations, plutovg reads fonts without `mmap` on the Switch, `tests/layerexdraw` in `run-tests.sh` | **30/30** GCC, **30/30** clang ASan+UBSan; NRO builds |

Lesson: never re-declare kk4s functions by hand in engine/plugin code; include `kk4s_platform.h` /
`kk4s_internal.h`. Unverified: layerExDraw output in a real game and on the Switch (fonts are indexed on
the first `GdiPlus.Font`: every font file is read once — with big CJK `.ttc` files on the SD that may be slow).

**Step 3: optional soft mode** (user's plan step 3). A TJS VM hook (`TJSSoftMissingMemberHook` in
`tjsInterCodeExec.cpp`, only `GetPropertyDirect` and `CallFunctionDirect/Indirect`) asks
`compat/kk4s_softmode.cpp`, which says yes only when `soft_mode=1`, a plugin was ignored in this game
(`PluginIgnoredThisGame`, reset in `BeforeStartup`) and the object is a native class object or an instance
of one (the native classes are collected from the global object at startup). Launcher toggle with
uk/en/ru/ja text naming the risk. `tests/softmode` runs both settings. **31/31** GCC and ASan+UBSan; NRO
builds. Not tried on a real game: whether it gets any stuck game further is unknown.

**Step 4: the game's own `.tpm` in an x86 interpreter** (user's plan step 2).

| Commit | What | Verified by |
|---|---|---|
| `c9f9999` | `compat/x86`: IA-32 interpreter (integer set, string ops, x87 control words only, cpuid without SSE, page-table memory, TIB, bump heap, host functions with cdecl / stdcall / guessed conventions) + PE32 loader (sections, relocations, imports, exports incl. `_name@N` / `name@N`) | `tests/tpm/x86_test.cpp`: `ops.c` at -O0/-O2/-Os + a relocated build, 12400/12400 calls each equal to native, also under clang ASan+UBSan with leak checks |
| `05a3f6a` | host table as a deque (a host function adding hosts reallocated the vector under itself) | ASan on `tests/tpm` |
| `1fd70ff` | `compat/x86/tpm_plugin`: fake Win32 (list in the file), fake `iTVPFunctionExporter` (both names APIs; convention guessed, then cdecl, then stdcall, each on a fresh machine), filter capture, `tTVPXP3ExtractionFilterInfo` pack(4) in 64 KiB pieces, `DetectXorModel` | `tests/tpm/tpm_test.cpp` |
| `30bfd6c` | `compat/kk4s_tpm.cpp` + `XP3Archive.cpp`: game `*.tpm` and `plugin/*.tpm` loaded once, bound by adlr before the keyless families; XOR filters natively from a cached 4 KiB keystream per file (2048 files max), others interpreted under a mutex | `tests/tpm/smoke.py` through the engine |
| `a4b0309` | synthetic filter plugins + tests, `run-tests.sh`, CI installs `gcc-mingw-w64-i686` | **34/34** GCC and clang ASan+UBSan; NRO builds |

Source of the ABI facts (krkr2, read from a temporary clone, nothing copied): `TJS_INTF_METHOD` is
`__stdcall` in 2.23's tp_stub and `__cdecl` since TJS2 2.4.14 / Kirikiri 2.25 beta 1; tp_stub's imported
functions and the filter callback are `__stdcall`; `tTVPXP3ExtractionFilterInfo` is `#pragma pack(4)`
(SizeOfSelf, Offset u64, Buffer, BufferSize, FileHash = 24 bytes). **Not verified on a real game's `.tpm`**
(none available here): real plugins are MSVC builds with a static CRT whose DllMain may call Win32
functions the fake API lacks; the log names the function (`the plugin called X (not provided by kk4s)`),
add it to `TpmPlugin::Win32`. Interpreter speed: ~35 M guest instructions/s on the dev PC.

**Step 5: open small items.**
- `e6d8e2c` + `8819c7b`: `tests/sprintf` (104 cases vs C printf) found three bugs in kk4s' own
  `TJS_snprintf`: `#` flag ignored (`%#x` gave `ff`), values of 20+ digits lost a digit (`%u` of -1,
  64-bit `%o`), `-INT64_MIN` UB. TJS' own rules stay as in Kirikiri 2 (checked in krkr2): real → string
  is `%.15g` with `+0.0` / `-0.0` / `NaN` / `±Infinity`; the format parser takes one flag only.
- `e352037`: ripple's vertical displacement **is there**; the earlier "no vertical displacement" note
  was wrong (it is only strong near the vertical axis through the centre). `tests/extrans` now checks
  the direction on 8 px stripes; with `yd` forced to 0 the test fails.
- BGRA audit of the built-in plugins: no further problems; the list is in `docs/plugins.md`
  ("Pixel order").

**NekoPara Vol. 1 on the PC viewer (2026-11-05, after stop point 3).** The title stopped at `title.ks:13`
on `Layer.loadImages("window@help__config%layer")`. Not a pseudo-storage: the archives' `eliF` tables list
`window@help__config%layer.png` in `patch.xp3`, which kk4s had not bound — all 222 entries carry adlr
`0x689792e4`, a constant, so no filter could be confirmed by adler32 although TinyFilter (key `0xAA` for that
adlr) decrypts it to PNG / PSB. Fixes, all generic: `1cd7bb3` (constant adlr → confirm by file signatures),
`730c6bf` (PSB chunk offset past the end, 01_01.ks.scn), `1f9dbb6` (textrender inline commands per KAGZ's
`textrender.tjs` header; `%fSourceHanSansCN-M;` was printed), `7828998` (two one-past reads in TJS string
helpers, found by the new test under ASan). Result: title, new game, the first scenes with the Russian patch,
A advances text. Seen only on the PC (headless screenshots). Also learned: `nekopara_vol1.tpm` is not an XP3
extraction filter; it registers an `iTVPStorageMedia`. The interpreter gets through its DllMain and most of
V2Link (`7c3f2ea`) but does not host storage media; NekoPara does not need it. Test state: **37/37** normal and
ASan+UBSan; the NRO builds. Never write probe output with a relative `file://./` path: it lands in the game
folder (happened once, cleaned up).

**E-mote investigation (2026-11-05; cipher solved, cross-game audit done).**
- NekoPara has no static character sprites: every character is an E-mote 3.0 PSB (`emote.xp3`, 79 files;
  `emotewin.xp3` holds `dx_` variants). Scripts drive it through `Motion.EmotePlayer` variables
  (`face_eye_open`, `head_LR`, `body_slant`, ...), `playTimeline`, `setVariable(name, value, time, easing)`.
- The PSB (v2) header is plain; `[header_len, off_chunk_offsets)` is encrypted, chunk tables and texture
  data are plain. Cipher: xorshift128, seeds 123456789 / 362436069 / 521288629, fourth word = decimal
  `742877301`, low byte first, new word when `cur` becomes 0. All 157 PSB in the three local archives
  decrypt, re-encrypt byte-identically and match `adler32`/TinyFilter: 157/157. Details:
  `docs/emote_psb_research.md`, `docs/emote_psb_validation.md`.
- The key is **not** a u32 immediate. File offset `0x4B399` is seed x (`0x075BCD15`); the key is the ASCII
  string `742877301` at `.rdata` file `0xD3794` / VA `0x100D4394`, pushed at VA `0x1004BF6D` and stored as
  the fourth state word at `[ebp-0xBC]`. (Earlier revisions of this note misread `0x4B399`.) The seeds at
  `0xB58FE`/`0xB5BDA` belong to a separate runtime RNG seeded from a call, not the PSB cipher.
- Cross-game audit: only NekoPara Vol.1 has E-mote artifacts locally. ATRI and FSN have no
  `emoteplayer.dll`/`emotedriver.dll`; both M2 `psbfile.dll` readers contain no cipher constants. No second
  game was verified, so portability of the key remains a hypothesis. Tools and full evidence:
  `research/emote-crossgame/`, `docs/emote_crossgame_compatibility.md`.
- Rendering: FreeMote's viewer calls the game's own emotedriver.dll; there is no open E-mote renderer.
  Plan: first a static pose (each layer's default frame, no mesh deformation), then variables / timelines.

**Universal E-mote runtime (2026-11-05, branch `feature/universal-emote-runtime`).** New module
`compat/emote/` (pure C++17 + zlib, in CMake for desktop and Switch, not yet used by `Motion.*`): PSB core
on top of `compat/psb`, evidence-based cipher profiles (the NekoPara key lives only in
`profiles/nekopara_vol1.cpp`), normalized Value and character models, an adapter keyed to the in-data
markers `version 3.03` + `metadata.format "emote"` + `spec krkr|win`, animation runtime (variables, tweens,
timelines), active-frame scene evaluation and a renderer contract. Real data: 157/157 NekoPara PSB load end to
end, 0 unresolved references. Not done: drawing, deformation, `RL`/`DXT5` pixels, `Motion.*` wiring. Design,
evidence and limits: `docs/universal_emote_runtime.md`.

**STOP POINT 2 (2026-11-05, user stopped the session; resolved in step 2 above).** Committed as work in progress and **not
built**: `engine/source/plugins/layerExDraw/` — layerExDraw.dll without GDI+: vendored plutovg (MIT,
`plutovg/UPSTREAM.txt`), `gdip_kk4s.h` (GDI+ enums with their real values, PointF / RectF / Matrix /
Image / GraphicsPath), `LayerExDraw.hpp/.cpp` (fonts from kk4s' font files + addPrivateFont, solid /
linear / radial(path-gradient) / texture brushes, pens with width / dashes / caps / joins, all path
shapes incl. cardinal curves and arcs, text outlines, drawImage*, transforms, SourceOver onto the
RGBA layer, getColorRegionRects, saveImage via saveLayerImage; EMF recording is a stub) and upstream
`main.cpp` (only GCC template fixes). It **compiles**, but `tests/layerexdraw/smoke.py` aborts with
`free(): invalid pointer` right after `Plugins.link`, before the first check. To continue:
1. Put the sources back into `CMakeLists.txt` (see the comment there: `LayerExDraw.cpp`, `main.cpp`,
   `plutovg/*.c`, plus `target_compile_definitions(... PLUTOVG_BUILD_STATIC PLUTOVG_BUILD)`), build
   `build-asan` and run `tests/tjs/layerexdraw` under it to get the stack. Suspects: ncbind's
   instance hook for `NCB_ATTACH_CLASS_WITH_HOOK(LayerExDraw, Layer)` (upstream creates the instance
   in the hook; our `LayerExDraw` has no `layerExBase` and a different destructor), `Appearance::DrawInfo`
   copies in `vector` growth (PaintInfo owns `Image*`), RectF boxing of return values.
2. Then `tests/layerexdraw` must pass (it calls every method with all arguments: ncbind, like
   upstream's, rejects missing ones) and go into `tools/run-tests.sh`, ASan, Switch build.

Remaining plan after that, in the user's order: step 2, the `.tpm` x86 interpreter (PE32 loader +
small x86 interpreter + fake `tp_stub` exporting `TVPSetXP3ArchiveExtractionFilter` etc., the
callback run per segment and verified by adlr like `compat/kk4s_xp3crypt.cpp`); step 3, the optional
soft mode for unknown plugins (missing members → log + void, behind a setting). Also open: ripple's
vertical displacement, extrans / saveStruct / KAGParserEx / psdfile under ASan, and every new plugin
on real games (none available in the cloud session).

**Earlier stop point (2026-11-05, kept for history).** Last commit `b644084` + this docs commit. Nothing is
half-done in the working tree. Open threads, in this order:
1. *Partly verified:* ripple. From a 16 px checkerboard, ~30% of the half-way samples lie on the
   wrong colour (displacement works; `tests/extrans` checks it against a cross-fade control), but
   8 px **horizontal** stripes showed no displacement at all — the vertical component may be
   missing. Look at the y part of the drift map / `TVPRippleTransform_*` arguments (temporary
   logs need `#include "DebugIntf.h"` for `TVPAddLog`); compare with a krkr2 screenshot if
   possible. Also unverified: the extrans pictures on the Switch, extrans under ASan.
2. Plan step 1 (native plugin contracts): saveStruct, csvParser, varfile, KAGParserEx, addFont,
   psdfile **done** (see the table). Remaining: **layerExDraw** (GDI+ drawing). Plan: vendor
   plutovg (MIT, sammycage/plutovg: paths, strokes with joins/caps/dashes, gradients, textures,
   matrices, glyph paths, a font cache that finds faces by family name) and write the GDI+-shaped
   classes `main.cpp` binds (PointF, RectF, Matrix, Image, Path, FontInfo, Appearance) over it;
   draw into a premultiplied scratch surface and composite onto the layer (straight alpha,
   RGBA). Fonts: `kk4s::SystemFontFiles()` + game fonts + `addPrivateFont`.
3. Plan step 2: x86 mini-interpreter for `.tpm` filters (fake `tp_stub`, adlr-verified).
4. Plan step 3: optional "soft mode" for missing members of unknown plugins (switch only).
Tasks for these exist in the session list as #12–#17.

**What was learned about Kirikiri / krkrs internals** (kept in the docs named):
- krkrs stores 32bpp layer images as RGBA bytes; Kirikiri and every Windows plugin assume BGRA.
  `fillRect`/`setMainPixel` convert with `TVP_REVRGB`; any native code on `mainImageBuffer` must
  too (`docs/plugins.md`, "Pixel order").
- `setMainPixel` with a non-zero high byte is a *system colour* (`clBtnFace` etc.) in Kirikiri;
  set ARGB pixels with `fillRect(x, y, 1, 1, argb)`. `getMainPixel` is colour only; alpha is
  `getMaskPixel`.
- PackinOne.dll is the wamsoft layerEx plugins (and others) statically linked into one DLL.
- Engine `saveLayerImage(name, "png"|"tlg5"|"tlg6"|"bmp")` writes through the storage layer, so
  the save-folder redirect applies; TLG tags come from the meta dictionary.
- json.dll does **not** accept unquoted keys (`{a:1}` is a parse error), only quoted ones.
- Original project-dir search order (engine `SysInitImpl.cpp`): `content-data/` → `data.xp3` →
  `data.exe` → XP3 appended to the exe itself → `data/` → exe folder.
- Windows input semantics games rely on: `onClick` is left-button only and goes to the layer that
  captured the press; mouse shifts include the held buttons; the wheel delta is a multiple of 120;
  `Layer.cursorX = …` / `setCursorPos` generates a mouse move (KAG3 uses it to highlight the link
  chosen with the arrow keys); Kirikiri itself auto-repeats pad buttons (`-paddelay` 500 ms,
  `-padinterval` 30 ms).
- KAG3's backlog: wheel up / `R` / Shift+Up open it; inside, wheel up scrolls back, wheel down at
  the end closes it, right click closes it. Its top-left corner is the "previous page" button.
- Scripted SDL mouse events need `SDL_VIDEODRIVER=dummy` (another fallback driver drops them).

**Unverified assumptions** (need a real game or hardware): that ATRI's affine layers look right
with the real `clipAlphaRect`; that alpha movies now show correct colours on screen; that games
whose save thumbnails use layerExSave save/load on the Switch (writes through the SD redirect).

**Local build/test commands used in the cloud session**:
```bash
bash tools/build-desktop.sh && bash tools/run-tests.sh                 # normal build + suite
cmake -S . -B build-asan -G Ninja -DKK4S_PLATFORM=desktop -DKK4S_ASAN=ON && cmake --build build-asan
ASAN_OPTIONS=detect_leaks=0 bash tools/run-tests.sh build-asan/kk4s   # what CI runs
# Switch NRO without CI: start a docker daemon if none runs, then use the devkitA64 image
dockerd --iptables=false --ip6tables=false --bridge=none --storage-driver=vfs &
docker run --rm -v "$PWD":/src -w /src devkitpro/devkita64:latest \
  bash -c 'git config --global --add safe.directory "*"; SKIP_DEPS=1 bash tools/build-switch.sh'
```
The image already contains every portlib the build needs (`SKIP_DEPS=1`); `pkg.devkitpro.org` was
not reachable from the cloud session.

**Next, in the order the user set on 2026-11-05** (also in `docs/roadmap.md`). No universal
Windows-DLL runtime (x86-32 on ARM64 + tp_stub ABI + Win32 is Wine/box86 territory); work by class:
1. **Native rewrites of popular plugin contracts**, like layerEx* / fstat / json / windowEx:
   extrans (real transitions instead of cross-fades), layerExDraw, psdfile, saveStruct, varfile,
   csvParser, KAGParserEx, addFont. Pick by real logs: `compat: plugin not available, ignored:
   <dll>` and `Member "…" does not exist`.
2. **A small x86 interpreter only for XP3 `.tpm` filters**: load the game's own `.tpm`, give it a
   fake `tp_stub` with the few functions such plugins call (`TVPSetXP3ArchiveExtractionFilter` …),
   capture the callback and run it sandboxed, verified against adlr like the keyless discovery.
   Keys stay in the game's files; nothing enters the repository.
3. **Optional "soft mode"** for unknown plugins: missing members log and return void instead of
   throwing. Only behind a switch (silent glitches in saves are the risk).
4. (Windows PC only, low priority) a 32-bit build that loads the real DLLs.
Smaller items still open: KAGEX / KAGZ fixtures if a redistributable copy is found; legacy formats
to check (`.sli` loops, `.tft` fonts, MIDI, ERI); `Font.registerFontFile` and other krkrz members
in `docs/api-coverage.md`.

Reference sources used in the cloud session (fetch again when needed, never commit game data):
`git clone --depth 1 https://github.com/krkrz/krkrz` (krkrz engine), `https://github.com/krkrz/krkr2`
(Kirikiri 2 history incl. `kag3/`), `https://github.com/wamsoft/<plugin>` (layerExImage,
layerExBtoA, layerExRaster, layerExSave, fstat, json, scriptsEx, windowEx, shrinkCopy, psdfile…).
krkr2's own plugins (extrans, KAGParserEx, csvParser, saveStruct, varfile, addFont…) are in
`krkr2/src/plugins/win32/` and `https://github.com/wamsoft/<plugin>`.

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
exercise that path; it is not an acceptance target by itself. (The `layerEx*` gap described in the
older notes is closed on `cloud-session` except layerExDraw.) Full status and crash analysis:
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

The desktop build also needs **capstone** (CX scheme recovery). On the dev box without root it comes
from the Fedora RPM, unpacked (2026-11-05; `capstone` itself is installed, only `-devel` is missing):
`dnf download capstone-devel` → `rpm2cpio … | cpio -idm` into `~/.local/kk4s-deps/root`, symlink
`libcapstone.so` → `/usr/lib64/libcapstone.so.5`, a `~/.local/kk4s-deps/pc/capstone.pc` pointing there.
Then configure with `PKG_CONFIG_PATH=~/.local/kk4s-deps/pc CXXFLAGS=-I$HOME/.local/kk4s-deps/root/usr/include`
(the sources include `<capstone/capstone.h>`). **ASan here: clang** — GCC's `libasan` is not installed,
clang's compiler-rt is: `CC=clang CXX=clang++ cmake -S . -B build-asan -G Ninja -DKK4S_PLATFORM=desktop
-DKK4S_ASAN=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo` (same env as above). To see sanitizer reports, which the
tests capture, run the suite through a wrapper that appends kk4s' stderr to a file, with
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`; symbolize with `addr2line -f -C -i -e build-asan/kk4s`.
The box has **sdl2-compat** (SDL2 API over SDL3); engine tests must run with `SDL_VIDEODRIVER=dummy`
(`run-tests.sh` sets it; a test started by hand without it opens a real window and the mouse lands 1 px off).

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

**Aspect, first (user report 2026-11-05):** Fate/FSN and Tsukihime now show 4:3 with black bars on the
16:9 screen (from `806395c`); before that they filled 16:9. Wanted: (a) a 16:9 picture for them by stretch
or an upscaler, and (b) a switch between original 4:3 and 16:9 (launcher setting, `settings.ini`).
Touch and mouse must keep mapping through whatever rectangle is shown (`kk4sGameRect` / `kk4sToGame`,
`tests/aspect`). Not committed yet; the code is unchanged in this step.

Then, in order:

1. Implement the remaining generic plugin APIs named by the logs: the KAGZ UI pseudo-storage
   (the NekoPara `%layer` blocker was a file in an unbound archive, fixed in `1cd7bb3`; layerExDraw and the layerEx*
   family are done on `cloud-session`). Do not special-case a game;
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
