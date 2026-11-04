# Plugin replacements

Windows plugins (`Plugins.link("x.dll")`) cannot run on Switch/Linux. kk4s replaces them natively.
Lookup order: engine internal plugins (`engine/source/plugins`) → kk4s table (`compat/kk4s_plugins.cpp`) → ignored with a log line `compat: plugin not available, ignored: x.dll`.

Status: **full** = same behaviour, **approx** = similar result, **stub** = does nothing (game logic continues, the effect is missing).

| DLL | Provides | Status |
|---|---|---|
| wuvorbis.dll | Ogg Vorbis decoding | full (engine decoder) |
| krmovie.dll | video playback (`VideoOverlay`) | kk4s FFmpeg player (`movie/kk4s_videoplayer.cpp`): overlay and layer modes, sound through the mixer, wall clock; mixer mode is drawn like overlay; contrast / hue controls are no-ops. Files that cannot be opened end at once instead of hanging |
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
| menu.dll | `Window.menu` + `MenuItem` | full: engine menu model (`MenuItemIntf.cpp`), items and hierarchy behave as in Kirikiri |
| | `Window.exSystemMenu` | approx: stored per window and readable by KAG's listeners, but there is no OS menu bar to display it in |
| windowEx.dll | `Window.registerExEvent` / `unregisterExEvent` | approx: kk4s already delivers mouse/keyboard events to `Window`, so this marks the window and returns true; no separate event mask |
| | `Window.disableResize`, `Window.disableMove` | approx: declared and stored, so KAGZ's plain assignments inside its methods work; kk4s windows have no OS decorations to drag, so there is nothing to disable |
| emoteplayer.dll / motionplayer.dll | `Motion.ResourceManager` | full: storage lookup, PSB parse (`compat/psb`), byte-budget LRU cache, unload/clearCache |
| | `Motion.Player` / `Motion.EmotePlayer` | partial: playback state machine (play/stop/skip/progress/time/playing, options, timelines list) is real; **E-mote rendering is not implemented** (`draw()` draws nothing, logged once per played motion), so emote animations appear static/absent. `MaskMode*`, `PlayFlag*`, `ShapeType*` are kk4s-internal values |
| | `Motion.SeparateLayerAdaptor` | stub (binder adaptor does nothing) |
| PackinOne.dll | `Layer.fillAlpha(alpha = 255)`, `Layer.clipAlphaRect(x, y, w, h, alpha = 255)` | full for the pixel semantics: fill the layer with `alpha`, or set it inside the rectangle and 0 outside, then update the layer; the argument count the game actually passes is logged on first use |

Next candidates: KAGZ UI layer storages (`<name>%layer` passed to `Layer.loadImages`, NekoPara's next blocker at `title.ks:13`), emoteplayer / motionplayer (`Motion.ResourceManager` and friends, needed by KAGZ titles such as NekoPara before their scripts finish initialising), layerExDraw / layerExImage / layerExRaster / layerExBtoA (ATRI's affine/PSD scene needs `Layer.fillAlpha` and `KAGLayer.clipAlphaRect`), `LayerExAffineSourceNoise.generateWhiteNoise`, layerExDraw / layerExImage / layerExRaster / layerExBtoA (the remaining affine/PSD primitives; alpha fill/clip is now in the PackinOne shim), layerExAlpha, real extrans transitions,
kagexopt, menu, fstat, scriptsEx.

How to add one: write a `Link...()` function (TJS shim string or native `tTJSDispatch` functions), add it to `kPlugins` in `compat/kk4s_plugins.cpp`, document it here. To see what a DLL registers, list its UTF-16 strings — they are the TJS names:

```bash
grep -aoP '(?:[\x20-\x7e]\x00){3,}' x.dll | tr -d '\0' | sort -u
```

Then check how the game calls them (global function or Layer method, which arguments) in its decrypted scripts — see the "probe" technique in `docs/HANDOFF.md`.
