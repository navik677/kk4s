# Plugin replacements

Windows plugins (`Plugins.link("x.dll")`) cannot run on Switch/Linux. kk4s replaces them natively.
Lookup order: engine internal plugins (`engine/source/plugins`) → kk4s table (`compat/kk4s_plugins.cpp`) → ignored with a log line `compat: plugin not available, ignored: x.dll`.

Status: **full** = same behaviour, **approx** = similar result, **stub** = does nothing (game logic continues, the effect is missing).

| DLL | Provides | Status |
|---|---|---|
| wuvorbis.dll | Ogg Vorbis decoding | full (engine decoder) |
| krmovie.dll | video playback | engine (FFmpeg), not tested yet |
| dirlist.dll | `getDirList()` | full (engine internal plugin) |
| csvParser, fftgraph, getSample, perspective, saveStruct, varfile, win32dialog, wutcwf | see `engine/README.upstream.md` | engine internal (krkrs) |
| util.dll | `GetFileSize`, `DeleteFile`, `MoveFile` | full |
| p.dll (fan patches) | `readFile(name)` → text or -1 | full |
| filter.dll | `Stretch` | full (Layer.operateStretch) |
| | `Blur`, `Smudge` | approx (box blur) |
| | `Contrast` | approx (gamma) |
| | `Noise` | random static |
| | `InitLens` / `Lens` / `ReleaseLens` | stub |
| | `initHaze` / `doHaze` / `endHaze` | approx (copies picture, no shimmer) |
| drawer.dll | `drawLine`, `drawAALine`, `drawAATriangle` | approx (no anti-aliasing, triangle outline) |
| slideopen.dll | `initSlideOpen` / `drawSlideOpen` / `finishSlideOpen` | approx (cross-fade) |
| firespark.dll | `*FireSpark` particles | stub |
| extrans.dll | transitions wave, ripple, mosaic, turn, rotate* | approx: engine falls back to crossfade (logged once per name) |

Next candidates: real extrans transitions, layerExDraw / layerExImage / layerExAlpha, kagexopt, menu, fstat, scriptsEx.

How to add one: write a `Link...()` function (TJS shim string or native `tTJSDispatch` functions), add it to `kPlugins` in `compat/kk4s_plugins.cpp`, document it here. To see what a DLL registers, list its UTF-16 strings — they are the TJS names:

```bash
grep -aoP '(?:[\x20-\x7e]\x00){3,}' x.dll | tr -d '\0' | sort -u
```

Then check how the game calls them (global function or Layer method, which arguments) in its decrypted scripts — see the "probe" technique in `docs/HANDOFF.md`.
