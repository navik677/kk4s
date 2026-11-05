# Native API coverage

Which members of the built-in TJS classes (`System`, `Window`, `Layer`, `Storages`, …) the kk4s
engine lacks compared with the reference engines, and what kk4s supplies instead. A script that
calls a member nobody provides stops with `Member "x" does not exist`, so this list is a map of
whole classes of games that can break.

Generated with `tools/native-api.py` (reads `TJS_BEGIN_NATIVE_MEMBERS` blocks; members added at
run time by kk4s' `compat/` shims are **not** seen by the tool, see the last column of each
section). Re-run it after engine changes:

```bash
git clone --depth 1 https://github.com/krkrz/krkr2  /tmp/krkr2
git clone --depth 1 https://github.com/krkrz/krkrz  /tmp/krkrz
python3 tools/native-api.py diff engine/source/core /tmp/krkr2/kirikiri2/trunk/kirikiri2/src/core --markdown
python3 tools/native-api.py diff engine/source/core /tmp/krkrz/common /tmp/krkrz/win32 --markdown
```

## Against Kirikiri 2 (krkr2 2.32, base of most KAG3 games)

| Class | Members missing here |
|---|---|
| Debug  | `visible` |
| System  | `inputString` |
| Window  | `menu` |

kk4s supplies all three at run time: `System.inputString` (Switch: system keyboard; PC: the
initial text or `$KK4S_INPUT_STRING`) and `Debug.visible` (always false) for every game
(`InstallEngineNativesCompat`), `Window.menu` through the engine menu model (`docs/plugins.md`,
menu.dll). Tested by `tests/natives` and `tests/kag3`.

## Against Kirikiri Z (krkrz master `7d91ba5`, 2026-10-20)

| Class | Members missing here |
|---|---|
| Agent (class missing) | `a11yAction`, `a11yLog`, `a11yTree`, `captureScreen`, `click`, `closeAllDialogs`, `closeDialog`, `dialogClick`, `dialogFocus`, `dialogTree`, `dialogs`, `ignoreRealMouse`, `imeStatus`, `keyDown`, `keyPress`, `keyUp`, `lastCapture`, `mouseDown`, `mouseMove`, `mouseUp`, `text`, `wheel` |
| BasicDrawDevice  | `captureWithOverlay`, `d3d11Device`, `dialogRendererHost`, `videoPresenterHost`, `viewportBackgroundHost` |
| Bitmap  | `imageHeight`, `imageWidth`, `mainImageBuffer`, `mainImageBufferForWrite`, `mainImageBufferPitch` |
| BitmapDrawDevice (class missing) | `bitmap`, `interface` |
| BitmapLayerTreeOwner  | `clearDirtyRect`, `dirtyRect`, `isUpdated`, `layerEventTargetInterface`, `update` |
| Canvas (class missing) | `beginEffect`, `beginMaskClip`, `beginStencilClip`, `blendMode`, `capture`, `clear`, `clearColor`, `clipRect`, `defaultFillShader`, `defaultShader`, `draw`, `draw9Patch`, `drawMesh`, `drawMesh2D`, `drawText`, `drawTexture`, `drawTextureAtlas`, `drawTransition`, `enableClipRect`, `enableCulling`, `endEffect`, `endMaskClip`, `endStencilClip`, `fill`, `flush`, `height`, `matrix`, `name`, `renderTarget`, `restore`, `save`, `width` |
| Clipboard  | `getAsBitmap`, `setAsBitmap` |
| Debug  | `prettyPrint` |
| ElementsDialog (class missing) | `a11yActive`, `a11yLabel`, `a11yLayers`, `a11yMode`, `activate`, `active`, `announce`, `atlasCacheBudget`, `atlasCacheStats`, `baseSize`, `beginKeyCapture`, `clearGameA11y`, `clearHotKeys`, `clearImages`, `clearSharedVars`, `close`, `defaultFontFamily`, `dictToJson`, `endKeyCapture`, `focus`, `focusRing`, `fontLanguages`, `getSharedVars`, `getVar`, `hasPhysicalKeyboard`, `language`, `listVars`, `modalActive`, `onAction`, `onClose`, `onKeyCapture`, `onScreen`, `onScreenLeave`, `partialRedraw`, `registerFont`, `registerFontDir`, `registerHotKey`, `registerImage`, `registerLayerImage`, `renderCache`, `renderCount`, `renderScale`, `renderStats`, `renderStatsReset`, `setAtlasImage`, `setGameA11y`, `setPadIconAlias`, `setPadIconBase`, `setPadTheme`, `setSharedVar`, `setVar`, `showDict`, `showFile`, `showFlow`, `showFlowScreens`, `showJson`, `showModalDict`, `showModalFile`, `showModalJson`, `startFlow`, `startFlowScreens`, `swappableAtlases`, `trimAtlasCache`, `unregisterHotKey`, `unregisterImage`, `virtualKeyboard`, `watchVars` |
| ElementsPanel (class missing) | `activate`, `active`, `close`, `focus`, `focused`, `getVar`, `invalidate`, `keyDown`, `keyUp`, `listVars`, `mouseDown`, `mouseLeave`, `mouseMove`, `mouseUp`, `mouseWheel`, `notifyResized`, `setVar`, `showDict`, `showFile`, `showJson`, `text`, `watchVars` |
| Font  | `colorEmojiFaceName`, `defaultEmojiMode`, `defaultUseVarStyle`, `emojiFaceName`, `emojiMode`, `getDefaultVariations`, `getFontInfo`, `getVarAxes`, `queryFonts`, `registerFontFile`, `setDefaultVariations`, `variations`, `weight` |
| GLCompositor (class missing) | `GLGetProcAddress`, `blendMode`, `canvas`, `capture`, `copyLayer`, `drawLayer`, `makeCurrent`, `screenHeight`, `screenWidth`, `setScreenSize`, `unpremultiply` |
| ImageFunction  | `copy9Patch` |
| Layer  | `drawShapedText`, `drawShapedTextArea`, `drawVerticalTextArea`, `measureShapedText`, `measureVerticalTextArea`, `shapedTextCount` |
| Matrix32 (class missing) | `array`, `multiply`, `preScale`, `premultiply`, `reset`, `rotate`, `scale`, `set`, `setRotate`, `setScale`, `setSkewX`, `setSkewY`, `setTranslate`, `skewX`, `skewY`, `transform`, `translate` |
| Matrix44 (class missing) | `add`, `array`, `div`, `frustum`, `lookAt`, `mul`, `mulVector`, `ortho`, `perspective`, `perspectiveFov`, `project`, `reset`, `rotate`, `rotateX`, `scale`, `set`, `setRotate`, `setRotateZ`, `setScale`, `setTranslate`, `sub`, `translate` |
| NullDrawDevice (class missing) | `interface`, `recreate` |
| OGLDrawDevice (class missing) | `canvas`, `createCanvas`, `dialogRendererHost`, `glVideoPresenterHost`, `interface`, `matrix`, `recreate`, `resumeOnDraw`, `texture`, `viewportBackgroundHost`, `window` |
| Offscreen (class missing) | `copyRect`, `exchangeTexture`, `height`, `name`, `nativeHandle`, `width` |
| SoundListener (class missing) | `enabled`, `setCone`, `setDirection`, `setPosition`, `setVelocity`, `setWorldUp` |
| Storages  | `addCacheTargetExtension`, `addDecodeTargetExtension`, `clearAllCaches`, `clearCache`, `clearFastCache`, `clearOldCache`, `clearStorageCaches`, `clearTransientCaches`, `copyFile`, `createDirectory`, `currentPath`, `deleteFile`, `dirlist`, `dirtree`, `dumpFileCacheList`, `dumpImageCacheList`, `exportFile`, `fstat`, `getFileCacheList`, `getImageCacheList`, `getMD5HashString`, `getTemporaryName`, `getTime`, `isCacheLoading`, `isCachePinned`, `isExistentDirectory`, `isExistentStorageNoSearchNoNormalize`, `isFastCacheLoading`, `isImagePrefetchLoading`, `moveFile`, `pinCache`, `removeDecodeTargetExtension`, `removeDirectory`, `requestCache`, `requestFastCache`, `setCacheMaxSize`, `truncateFile`, `unpinCache` |
| System  | `breathe`, `buildVariantName`, `captureScreen`, `createAppLock`, `expandEnvString`, `getAboutString`, `getLicenseList`, `getLicenseText`, `isGeneric`, `openGLESVersion`, `platformTag`, `readEnvValue`, `registerHotKey`, `renderStats`, `renderStatsReset`, `replActive`, `systemLanguage`, `texUploadUsePBO`, `uiLanguage`, `uiLanguageCandidates`, `unregisterHotKey`, `urldecode`, `urlencode`, `writeEnvValue` |
| Texture (class missing) | `copyRect`, `copyTexture`, `height`, `isGray`, `isPowerOfTwo`, `margin9Patch`, `memoryHeight`, `memoryWidth`, `name`, `nativeHandle`, `setDrawSize`, `stLinear`, `stNearest`, `stretchType`, `width`, `wmClampToEdge`, `wmMirror`, `wmRepeat`, `wrapModeHorizontal`, `wrapModeVertical` |
| TextureLayerTreeOwner (class missing) | `fireClick`, `fireDisplayRotate`, `fireDoubleClick`, `fireKeyDown`, `fireKeyPress`, `fireKeyUp`, `fireMouseDown`, `fireMouseMove`, `fireMouseOutOfWindow`, `fireMouseUp`, `fireMouseWheel`, `fireMultiTouch`, `fireRecheckInputState`, `fireReleaseCapture`, `fireTouchDown`, `fireTouchMove`, `fireTouchRotate`, `fireTouchScaling`, `fireTouchUp`, `focusedLayer`, `height`, `layerEventTargetInterface`, `layerTreeOwnerInterface`, `onChangeLayerImage`, `onDisableAttentionPoint`, `onGetCursorPos`, `onReleaseMouseCapture`, `onResetImeMode`, `onResizeLayer`, `onSetAttentionPoint`, `onSetCursorPos`, `onSetHintText`, `onSetImeMode`, `onSetMouseCursor`, `primaryLayer`, `texture`, `update`, `width` |
| VertexBinder (class missing) | `componentCount`, `name`, `normalize`, `offset`, `stride`, `vertexBuffer` |
| VertexBuffer (class missing) | `dataType`, `dtByte`, `dtFixed`, `dtFloat`, `dtInt`, `dtShort`, `dtUByte`, `dtUInt`, `dtUShort`, `isIndex`, `lock`, `name`, `nativeHandle`, `ptLineLoop`, `ptLineStrip`, `ptLines`, `ptPoints`, `ptTriangleFan`, `ptTriangleStrip`, `ptTriangles`, `setVertex`, `size`, `unlock`, `updateType`, `utDynamic`, `utStatic`, `utStream` |
| WaveSoundBuffer  | `attenuationModel`, `dopplerFactor`, `maxDistance`, `minDistance`, `rolloffFactor`, `set3DCone`, `set3DConeDirection`, `set3DPosition`, `set3DVelocity`, `setGainQueryCallback`, `use3D` |
| WebServer (class missing) | `active`, `broadcast`, `openBrowser`, `register`, `registerPanel`, `serveStatic`, `start`, `startAt`, `stop`, `unregister`, `unregisterPanel`, `unserveStatic`, `url` |
| Window  | `GLGetProcAddress`, `aspectLock`, `clearViewportWallpaper`, `disableMove`, `disableResize`, `displayDensity`, `frameHeight`, `frameWidth`, `getClientRect`, `getCursorPos`, `getNormalRect`, `getWindowRect`, `layerEventTarget`, `maximize`, `maximized`, `minimize`, `minimized`, `mouseCursor`, `onMaximizeQuery`, `registerDeviceChange`, `registerExEvent`, `requestUpdate`, `setClientRect`, `setCursorPos`, `setViewport`, `setViewportWallpaper`, `showRestore`, `viewportAlignX`, `viewportAlignY`, `viewportBgColor`, `viewportFit`, `viewportOffsetX`, `viewportOffsetY`, `viewportZoom` |

What this means for games, by group:

| Group | State in kk4s |
|---|---|
| `Storages.dirlist`, `fstat`, `getTime`, `copyFile`, `moveFile`, `truncateFile`, `getMD5HashString`, … (fstat.dll contract, native in krkrz) | supplied for every game at startup (`InstallEngineNativesCompat` → fstat replacement); `tests/fstat`, `tests/natives` |
| `Window.maximize`, `getClientRect`, `getWindowRect`, `registerExEvent`, `disableResize`, … (windowEx.dll contract) | supplied for every game; `tests/windowex`, `tests/natives` |
| `System.readEnvValue`, `expandEnvString`, `getAboutString`, `breathe`, `registerHotKey`, `systemLanguage`, `uiLanguage`, `writeEnvValue` | supplied for every game (languages answer as a Japanese PC: `ja-JP` / `ja`) |
| `System.createAppLock` | supplied by kk4s compat (KAG3 calls it; `tests/kag3`) |
| `System.urlencode` / `urldecode`, `captureScreen`, `getLicenseList/Text`, `renderStats`, `openGLESVersion`, `isGeneric`, `platformTag`, `buildVariantName`, `replActive`, `texUploadUsePBO` | missing; recent krkrz additions, not yet seen in games |
| `Layer.drawShapedText*`, `measureShapedText`, `drawVerticalTextArea` (text shaping) | missing; recent krkrz additions |
| `Font.weight`, `getFontInfo`, `queryFonts`, `registerFontFile`, variations / emoji | missing; `registerFontFile` would matter for games that bundle fonts that way (kk4s registers game-folder fonts by itself) |
| `Bitmap.imageWidth`/`imageHeight`/`mainImageBuffer*`, `Clipboard.getAsBitmap`/`setAsBitmap`, `Debug.prettyPrint`, `ImageFunction.copy9Patch`, `BitmapLayerTreeOwner.update` & co. | missing |
| `WaveSoundBuffer` 3D audio (`use3D`, `set3DPosition`, …), `SoundListener` | missing |
| `Canvas`, `Texture`, `Offscreen`, `Matrix32/44`, `VertexBuffer`, `OGLDrawDevice`, `GLCompositor` (krkrz 2.x GPU API) | missing; games using the GPU canvas API cannot run yet |
| `Agent`, `ElementsDialog`, `ElementsPanel`, `WebServer`, viewport/wallpaper members | missing; krkrz developer/UI tooling, not game API |
