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
| fstat.dll | `Storages.fstat` (size + `mtime`/`atime`/`ctime` Dates; size only inside an archive), `getTime` / `setTime`, `get/setLastModifiedFileTime` (FILETIME), `dirlist` / `dirlistEx` / `dirtree` (trailing `/` required, `./` and `../` listed as FindFirstFile does), `createDirectory[NoNormalize]`, `removeDirectory`, `isExistentDirectory`, `copyFile[NoNormalize]`, `exportFile`, `deleteFile`, `moveFile`, `truncateFile`, `get/set/resetFileAttributes`, `getMD5HashString`, `getTemporaryName`, `isExistentStorageNoSearchNoNormalize`, `clearStorageCaches`, `TemporaryFiles`, `FILE_ATTRIBUTE_*` | full, with these differences: `fstat` of a missing file returns void (the DLL throws; kk4s always did this and util.dll games use it); `ctime` is the POSIX change time; attributes are derived (READONLY = not writable, HIDDEN = dot file) and only READONLY can be changed; `changeDirectory` only checks the folder; `getDisplayName` is the file name; `searchPath` returns void; `selectDirectory` returns false; nothing that writes (times, attributes, truncate, `TemporaryFiles` deletion) touches the game folder |
| json.dll | `Scripts.evalJSON(text)`, `evalJSONStorage(file, utf8)`, `saveJSON(file, obj, utf8, newline)`, `toJSONString(obj, newline)` | full (`compat/kk4s_json.cpp`): the DLL's lenient reader (comments `#` `//` `/* */`, `=`/`=>`, `;`, single quotes, `\x`, `true`/`false`/`null`/`void`; a number is Real only with `.`; unquoted keys are an error, as in the DLL) and its writer layout. Difference: `saveJSON` writes UTF-8 also when `utf8` is false |
| scriptsEx.dll | `Scripts.getObjectKeys`, `getObjectCount`, `getObjectContext`, `isNullContext`, `equalStruct`, `equalStructNumericLoose`, `foreach`, `clone`, `getMD5HashString(octet)`, `propSet`/`propGet` + `pf*` flags, `rehash`, `safeEvalStorage` | full except `stringFuzzySearch` (missing). Most members are installed for every game at startup; `safeEvalStorage` on link |
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
| saveStruct.dll | `saveStruct2`, `toStructString`, `Scripts.toStructString`, `save2`, `sso*` options | **engine internal plugin**, current wamsoft contract (was an old copy that wrote raw UTF-16 and returned an unterminated string); files are UTF-8 in both modes (Windows used the ANSI code page for `utf = false`) |
| extrans.dll | transitions wave, mosaic, turn, rotatezoom, rotatevanish, rotateswap, ripple | **engine internal plugin** (`engine/source/plugins/extrans/`, krkrz extrans ported: scan lines read from krkrs' software textures, option colours converted to RGBA order, the turn table computed at start-up, ripple's `char` casts made signed for ARM). `tests/extrans` checks each picture half-way (ripple: displacement seen on a checkerboard, its vertical part unverified). Unknown names still fall back to crossfade |
| menu.dll | `Window.menu` + `MenuItem` | full: engine menu model (`MenuItemIntf.cpp`), items and hierarchy behave as in Kirikiri |
| | `Window.exSystemMenu` | approx: stored per window and readable by KAG's listeners, but there is no OS menu bar to display it in |
| windowEx.dll | `Window.registerExEvent` / `unregisterExEvent` | approx: kk4s already delivers mouse/keyboard events to `Window`, so this marks the window and returns true; no separate event mask |
| | `Window.disableResize`, `disableMove`, `maximized`, `minimized`, `enableNCMouseEvent` | stored values: there are no OS window decorations to act on |
| | `Window.getClientRect` / `getWindowRect` / `getNormalRect` / `setClientRect` | full for a frameless window (client rect = window rect) |
| | `Window.maximize`, `minimize`, `showRestore`, `bringTo`, `sendToBack`, `setWindowIcon`, `resetWindowIcon`, `setWindowCornerPreference`, `acquireImeControl`, `resetImeContext`, `focusMenuByKey`, `setMessageHook`, `getNotificationNum/Name`, `setOverlayBitmap`, `registerHotKey`, `registerDeviceChange`; `ncHitTest` (= `nchtClient`) and the `ncht*` constants | no-ops / fixed answers (nothing to act on) |
| | `System.getMonitorInfo`, `getDisplayMonitors` (with `intersect`), `getSystemMetrics` (`SM_*` names, Windows 10 defaults for frames/icons, the screen for sizes, 0 for unknown), `getDoubleClickTime` (500) | as Windows with one monitor of `System.screenWidth` x `screenHeight` |
| | `System.readEnvValue`, `expandEnvString` | APPDATA, LOCALAPPDATA, USERPROFILE, HOMEPATH, TEMP, TMP, ALLUSERSPROFILE, PROGRAMDATA, PUBLIC = the game's save folder (so saves built from them stay out of the game folder); OS = `Windows_NT`, USERNAME, COMPUTERNAME fixed; others from the process environment (desktop) or void; unknown `%NAME%` stays as is |
| | `System.getCursorPos` / `setCursorPos`, `setClipCursor`, `breathe`, `isBreathing`, `clearGraphicCache`, `getAboutString`, `getCPUType`, `mapVirtualKey`, `setApplicationIcon`, `setIconicPreview`, `setDpiAwareness`, `findWindowEx`, `classLongPtr`, `loadCursor`; `Scripts.setEvalErrorLog`; `MenuItem.popupEx` + `bi*` / `rightJustify` / `bmpChecked` / `bmpUnchecked`; `Pad.registerExEvent`; `Debug.console` helpers | cursor through the main window's primary layer; the rest no-ops or fixed values |
| emoteplayer.dll / motionplayer.dll | `Motion.ResourceManager` | full: storage lookup, PSB parse (`compat/psb`), byte-budget LRU cache, unload/clearCache |
| | `Motion.Player` / `Motion.EmotePlayer` | partial: playback state machine (play/stop/skip/progress/time/playing, options, timelines list) is real; **E-mote rendering is not implemented** (`draw()` draws nothing, logged once per played motion), so emote animations appear static/absent. `MaskMode*`, `PlayFlag*`, `ShapeType*` are kk4s-internal values |
| | `Motion.SeparateLayerAdaptor` | stub (binder adaptor does nothing) |
| PackinOne.dll | the layerEx plugins below, bundled into one DLL | links layerExBtoA + layerExImage + layerExRaster + layerExSave (`compat/kk4s_layerex.cpp`) |
| layerExBtoA.dll | `Layer.copyRightBlueToLeftAlpha`, `copyBottomBlueToTopAlpha`, `fillAlpha()`, `copyAlphaToProvince(threshold, matched = 1, otherwise = 0)`, `clipAlphaRect(dleft, dtop, src, sleft, stop, swidth, sheight, clear)` (incl. `ltAddAlpha` layers), `overwrapRect(..., threshold = 1)`, `fillByProvince(index, color)`, `fillToProvince(color, index, threshold = 64)` | full: argument lists, clip-rect rules and pixel maths of the wamsoft sources. Differences: a layer without an image is logged and skipped instead of throwing; `overwrapRect` also calls `update()` (the DLL forgets it) |
| layerExImage.dll | `Layer.light`, `colorize`, `modulate`, `noise`, `generateWhiteNoise`, `gaussianBlur(radius = 1)` | full: CxImage-derived maths of the original, on the clip rect; noise uses a fixed-seed generator instead of the CRT `rand()` |
| layerExSave.dll | `Layer.saveLayerImagePng(file, tags)`, `saveLayerImageTlg5(file, tags)`, `saveLayerImagePngOctet()`, `getCropRect`, `getCropRectZero`, `getDiffRect`, `getDiffPixel`, `oozeColor`, `copyBlueToAlpha`, `isBlank`, `clearAlpha`, `getAverageColor`; `Window.startSaveLayerImage` / `cancelSaveLayerImage` / `stopSaveLayerImage` | full for the images (the engine's PNG / TLG writers, through the save redirect; TLG tags are written). Differences: PNG tags / `comp_lv` are not written; `startSaveLayerImage` saves the copy at once and sends `onSaveLayerImageProgress(…, 100, …)` and `onSaveLayerImageDone` on the next frame. Not implemented: `saveProvinceImage`, `getFingerPrintValue`, `getShrinkVectorOctet`, `Math.octetVectorSum`. Quirks kept from the DLL: `isBlank` tests the blue byte only, `getAverageColor` packs B,G,R,A |
| shrinkCopy.dll | `Layer.shrinkCopy(dleft, dtop, dwidth, dheight, src, sleft, stop, swidth, sheight)` (area average, fractional destination, enlarging is an error), `Layer.shrinkCopyFast(src, shrink_x, shrink_y)` (integer block average, resizes the layer, opaque result) | full for the results; edge pixels of a fractional destination are written whole, not blended |
| layerExAreaAverage.dll | `Layer.stretchCopyAA(...)` | area average as shrinkCopy (also when enlarging) |
| layerExRaster.dll | `Layer.copyRaster(src, maxh, lines, cycle, time)` | full (raster wave copy); `lines = 0` / `cycle = 0` are guarded instead of dividing by zero |

Next candidates: KAGZ UI layer storages (`<name>%layer` passed to `Layer.loadImages`, NekoPara's next blocker at `title.ks:13`), emoteplayer / motionplayer (`Motion.ResourceManager` and friends, needed by KAGZ titles such as NekoPara before their scripts finish initialising), layerExDraw (GDI+ drawing API), layerExSave, layerExAlpha, 
kagexopt, minizip, win32ole.

**Pixel order.** krkrs keeps layer pixels as RGBA bytes (uint32 `0xAABBGGRR`, the SDL texture is
`ABGR8888`); Windows plugins address BGRA. Native replacements that touch `mainImageBuffer` must
use the R/B offsets of `compat/kk4s_layerex.cpp` (`kR`/`kG`/`kB`/`kA`) and convert TJS colours
(`0xAARRGGBB`) with the engine's `TVP_REVRGB`. `fillRect` / `setMainPixel` / `getMainPixel` do this
conversion themselves. Note that `setMainPixel` with a non-zero high byte means a *system colour*
in Kirikiri, so set an ARGB pixel with `fillRect(x, y, 1, 1, argb)`.

How to add one: write a `Link...()` function (TJS shim string or native `tTJSDispatch` functions), add it to `kPlugins` in `compat/kk4s_plugins.cpp`, document it here. To see what a DLL registers, list its UTF-16 strings — they are the TJS names:

```bash
grep -aoP '(?:[\x20-\x7e]\x00){3,}' x.dll | tr -d '\0' | sort -u
```

Then check how the game calls them (global function or Layer method, which arguments) in its decrypted scripts — see the "probe" technique in `docs/HANDOFF.md`.
