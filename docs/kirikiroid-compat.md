# Kirikiroid2 compatibility

Many game folders on the internet are prepared for **Kirikiroid2** (Android). kk4s runs those
folders as-is: it supports the helper files Kirikiroid2 uses instead of Windows DLLs. The original
game files are never changed.

Code: `compat/kk4s_compat.cpp` (hooks), `compat/kk4s_plugins.cpp` (plugin replacements).

## Implemented

| File / API | What kk4s does |
|---|---|
| `xp3filter.tjs` in the game folder | Executed right before `startup.tjs`. |
| `Storages.setXP3ArchiveExtractionFilter(func)` | Registers `func(hash, offset, buf, len)`, called for every block read from an XP3 archive (after decompression). `buf` supports `buf[i]` (get/set/`^=`), `buf.xor(start, len, value)` (native, fast), `buf.ptr`, `buf.count`. `void` removes the filter. |
| `Override2.tjs` | Executed right after the game's `Override.tjs` (or after `MainWindow.tjs` if there is none). |
| `AfterInit2.tjs` | Executed after the game's `AfterInit.tjs` (or after `Initialize.tjs` / `startup.tjs`). |
| `Plugins.link("x.dll")` | Built-in replacement if there is one ([plugins.md](plugins.md)), otherwise ignored with `compat: plugin not available, ignored: x.dll`. |

Each helper script runs at most once; errors in it are logged and do not stop the game.

## Threads

TJS is not thread-safe, but audio decoders read archive files on their own threads. While a script
filter is set, every archive file opened on the main thread (sounds are opened from scripts, so
on the main thread) is read and decrypted completely at once (`XP3Archive.cpp`, files up to 64 MiB);
decoder threads then read memory. If the filter is ever called on another thread, the log says
`compat: xp3 filter script called on a background thread (unsafe)`.

## Not supported yet

- `cxdec.tpm` and other `.tpm` files (Windows decryption plugins) are ignored. A built-in x86
  interpreter is planned so encrypted games work without `xp3filter.tjs`.

## Tested

Fate/stay night (Realta Nua), Sharin no Kuni, Tsukihime + Plus-Disc, The Makings of a Lady — all
with Russian fan translations, see [status.md](status.md).

Rule: never commit scripts or data from real games. Tests use synthetic data (`tests/`).
