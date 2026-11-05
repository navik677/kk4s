# Roadmap

Full requirements: [original-prompt.md](original-prompt.md). Risk = chance of a big time sink.

| # | Milestone | Status | Risk |
|---|---|---|---|
| M0 | Repo structure, CI build of `kk4s.nro`, launcher, log | **done** | low |
| M1 | Runs real games on hardware; PC viewer (Linux + Windows); aspect-correct scaling | **partly** — games run on hardware; letterboxing + touch/mouse mapping (`tests/aspect`) works, but Fate/FSN and Tsukihime come out 4:3 with bars (user wants 16:9); 16:9 options below | medium |
| M2 | Storage: case-insensitive lookup, save redirect, overlay dir over xp3, memory-bounded caches | **partly** — lookup + saves done, overlay dir todo | medium |
| M3 | Game detection by hash + `profiles/<game_id>.json`; diagnostic report for unknown games | todo | low |
| M4 | Plugin shim: wuvorbis, krmovie, layerEx*, extrans, stubs for the rest | **partly** — layerExBtoA / layerExImage / layerExRaster / layerExSave (and PackinOne, which bundles them), fstat, json, scriptsEx, windowEx, shrinkCopy, layerExAreaAverage, menu, Motion model done; extrans done (`tests/extrans`); saveStruct, varfile, csvParser, KAGParserEx, addFont, psdfile, layerExDraw done (2026-11-05); KAGZ `%layer` storage open (see [plugins.md](plugins.md)). No universal Windows-DLL runtime | **high** |
| M5 | Decryption: Kirikiroid2 `xp3filter.tjs` (done), CXDEC schemes + scheme-free TinyFilter (done, validated per archive), keyless `.tpm` families discovered by adlr (done on `cloud-session`), the game's own `.tpm` filter run in a small x86 interpreter with a fake `tp_stub` (done on `cloud-session`, synthetic plugins only) | **partly** | medium |
| M6 | Core compat (System/Window/Layer/...), TJS conformance tests, profile hooks, CJK/Cyrillic fonts | **partly** — fonts done | high |
| M7 | Video (FFmpeg, alpha movies), audio semantics | todo (audio works, video untested) | high |
| M9 | E-mote: universal runtime (`compat/emote/`, [universal_emote_runtime.md](universal_emote_runtime.md)) | **partly** — PSB core, evidence-based cipher profiles, character model, animation runtime and renderer contract done and tested (157/157 NekoPara PSB); **not drawn**: needs the mesh / transform / mask semantics, the `RL` icon codec and `DXT5` atlases, and wiring into `Motion.EmotePlayer`; NekoPara stops after ~9 s on the PC (`Image Load Error`) | **high** |
| M10 | Kirikiroid2-pack compatibility (`patch.tjs`, `xp3filter.tjs`, stub classes) and Windows file names | **partly** — `patch.tjs`, fstat / Steam / layerExParticle, UTF-8 manifest done (2026-11-05, 10 of the 13 PC games tried start; NekoPara, Steins;Gate and UsotsukiGenius do not); Steins;Gate (RU beta) and UsotsukiGenius open; ZIP archives named `.xp3` skipped, not read | medium |
| M8 | Input mapping, touch, graphical launcher with covers, in-game overlay menu | **done** — Windows mouse/wheel rules, pad wheel + key repeat on `cloud-session` (`tests/input`) | low |

## Compatibility gaps found in the 2026-11-05 sprint (by how many games they can affect)

| Gap | Affects | State |
|---|---|---|
| Launcher lists only folders with `startup.tjs` / `*.xp3` at the top; `data/`-only folders, `data.exe` and XP3 appended to an exe are not listed or not resolved | unpacked / dev builds, many small doujin releases shipped as one exe | **done** (cloud-session `9d974ca`) |
| windowEx `System.getMonitorInfo`, `getSystemMetrics`, `getCursorPos`, … missing (calls throw) | KAGEX/KAGZ titles and others that size their window from the monitor | **done** (`f3923a0`) |
| Encrypted XP3 without `xp3filter.tjs`: only CXDEC / TinyFilter are recognised | many doujin and older commercial games with simple XOR `.tpm` filters | **done for keyless families** (`compat/kk4s_xp3crypt.cpp`, 18 synthetic schemes) and **for the game's own `.tpm`** run in the x86 interpreter (`compat/kk4s_tpm.cpp`, synthetic plugins; no real `.tpm` tried); adlr-after-encryption schemes and plugins needing files / threads / SSE still need a helper file |
| No end-to-end test of the KAG3 / KAGEX / KAGZ system scripts | every KAG game (YesNoDialog `showModal`, message layers, save/load) | KAG3: **done** (`tests/kag3`: found a window UAF; save + load; `interact_smoke.py`: history, hide, auto, skip with the pad); KAGEX / KAGZ: todo |
| Input differed from Windows Kirikiri: right/middle releases sent `onClick`, wheel delta 1 instead of 120, no button state in shifts / `getKeyState(VK_LBUTTON)`, `cursorX/Y` in window pixels, pad L = Page Up (no KAG3 backlog), no pad key repeat | every game (backlogs, buttons, drags, sliders) | **done** (`82bee15`, `cd79ed5`) |
| A 4:3 game is stretched to the 16:9 screen (old) or letterboxed (now) | every 4:3 game; Fate/FSN and Tsukihime on hardware (2026-11-05) show 4:3 with bars, the user wants 16:9 | **partly** (setting "Keep aspect ratio", default on). Planned: (a) 16:9 stretch or upscaler for the picture, (b) a launcher switch: original 4:3 / 16:9 |
| Native members of built-in classes that krkrs lacks compared with krkrz / krkr2 | any game calling them | **diffed** (`tools/native-api.py`, `docs/api-coverage.md`); fstat / windowEx / inputString sets done, the rest listed there |
| extrans transitions are cross-fades | KAG3 games using `[trans method=wave|mosaic|turn|rotate*]` | **done** (krkrz extrans ported into the engine, `tests/extrans`; ripple's picture unverified) |
| layerExDraw (GDI+ drawing) ignored | games drawing UI or text effects with it | **done** (plutovg; `tests/layerexdraw`, ASan+UBSan; approximations in plugins.md); not checked against a real game |
| A missing member of an ignored plugin throws and usually ends the game | games with plugins kk4s lacks | **optional soft mode** (off by default, `tests/softmode`); the real fix stays a native plugin |
| MIDI BGM, `.tft` pre-rendered fonts, ERI images, `.sli` loop files — not yet checked | older Kirikiri 2 titles | investigate |

