# Roadmap

Full requirements: [original-prompt.md](original-prompt.md). Risk = chance of a big time sink.

| # | Milestone | Status | Risk |
|---|---|---|---|
| M0 | Repo structure, CI build of `kk4s.nro`, launcher, log | **done** | low |
| M1 | Runs real games on hardware; PC viewer (Linux + Windows); aspect-correct scaling | **mostly done** — scaling missing | medium |
| M2 | Storage: case-insensitive lookup, save redirect, overlay dir over xp3, memory-bounded caches | **partly** — lookup + saves done, overlay dir todo | medium |
| M3 | Game detection by hash + `profiles/<game_id>.json`; diagnostic report for unknown games | todo | low |
| M4 | Plugin shim: wuvorbis, krmovie, layerEx*, extrans, stubs for the rest | **partly** — layerExBtoA / layerExImage / layerExRaster / layerExSave (and PackinOne, which bundles them), fstat, json, scriptsEx, menu, Motion model done; windowEx System/Window members, layerExDraw, shrinkCopy, real extrans, KAGZ `%layer` storage open (see [plugins.md](plugins.md)) | **high** |
| M5 | Decryption: Kirikiroid2 `xp3filter.tjs` (done), CXDEC schemes + scheme-free TinyFilter (done, validated per archive), `.tpm` decryptors via a built-in x86 interpreter (todo) | **partly** | medium |
| M6 | Core compat (System/Window/Layer/...), TJS conformance tests, profile hooks, CJK/Cyrillic fonts | **partly** — fonts done | high |
| M7 | Video (FFmpeg, alpha movies), audio semantics | todo (audio works, video untested) | high |
| M8 | Input mapping, touch, graphical launcher with covers, in-game overlay menu | **done** | low |

## Compatibility gaps found in the 2026-11-05 sprint (by how many games they can affect)

| Gap | Affects | State |
|---|---|---|
| Launcher lists only folders with `startup.tjs` / `*.xp3` at the top; `data/`-only folders, `data.exe` and XP3 appended to an exe are not listed or not resolved | unpacked / dev builds, many small doujin releases shipped as one exe | **done** (cloud-session `9d974ca`) |
| windowEx `System.getMonitorInfo`, `getSystemMetrics`, `getCursorPos`, … missing (calls throw) | KAGEX/KAGZ titles and others that size their window from the monitor | **done** (`f3923a0`) |
| Encrypted XP3 without `xp3filter.tjs`: only CXDEC / TinyFilter are recognised | many doujin and older commercial games with simple XOR `.tpm` filters | investigate: detect simple schemes by known plaintext, validate with `adlr` |
| No end-to-end test of the KAG3 / KAGEX / KAGZ system scripts | every KAG game (YesNoDialog `showModal`, message layers, save/load) | KAG3: **done** (`tests/kag3`, found a window UAF); KAGEX / KAGZ, bookmark load, history, auto/skip: todo |
| A 640x480 game is stretched to the 16:9 screen (no aspect-correct scaling) | every 4:3 game | todo (M1) |
| Native members of built-in classes that krkrs lacks compared with krkrz / krkr2 | any game calling them | todo: systematic diff |
| extrans transitions are cross-fades | KAG3 games using `[trans method=wave|mosaic|turn|rotate*]` | todo |
| layerExDraw (GDI+ drawing) ignored | games drawing UI or text effects with it | todo |
| MIDI BGM, `.tft` pre-rendered fonts, ERI images, `.sli` loop files — not yet checked | older Kirikiri 2 titles | investigate |

