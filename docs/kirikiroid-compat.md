# Kirikiroid2 compatibility

Many game folders on the internet are already prepared for **Kirikiroid2** (Android). kk4s runs those folders as-is: it supports the same helper files Kirikiroid2 uses instead of Windows DLLs. The original game files are never changed.

Code: `compat/kk4s_compat.cpp` (hooks), `compat/kk4s_plugins.cpp` (plugin replacements).

## Implemented

| File / API | What kk4s does |
|---|---|
| `xp3filter.tjs` in the game folder | Executed right before `startup.tjs`. |
| `Storages.setXP3ArchiveExtractionFilter(func)` | Registers `func(hash, offset, buf, len)`; the engine calls it for every block read from an XP3 archive (after decompression). `buf` supports `buf[i]` (get/set/`^=`), `buf.xor(start, len, value)`, `buf.ptr`, `buf.count`. `void` removes the filter. |
| `Override2.tjs` | Executed right after the game's `Override.tjs` (or after `MainWindow.tjs` if the game has no `Override.tjs`). |
| `AfterInit2.tjs` | Executed after the game's `AfterInit.tjs` (or after `Initialize.tjs` / `startup.tjs`). |
| `Plugins.link("x.dll")` | Built-in kk4s replacement if there is one, otherwise ignored with a log line `compat: plugin not available, ignored: x.dll`. |

Each helper script runs at most once; errors in it are logged and do not stop the game.

## Notes

- The filter can be called from audio/image loader threads. Calls are serialized; the TJS function should only touch `buf` and its locals (all filters seen so far do).
- `cxdec.tpm` and other `.tpm` files (Windows decryption plugins) are ignored for now. Running them through a built-in x86 interpreter is planned so encrypted games work even without `xp3filter.tjs`.

## Tested

- Fate/stay night (Realta Nua + fan translation, Kirikiroid2-ready folder): boots to the intro on the PC viewer (Windows). Missing plugins at that point: wuvorbis, extrans, drawer, filter, slideopen, firespark, dirlist.

Rule: never commit scripts or data from real games. Tests use synthetic data (`tests/`).
