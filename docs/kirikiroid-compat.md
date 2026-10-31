# Kirikiroid2 compatibility

Many game folders on the internet are prepared for **Kirikiroid2** (Android). kk4s runs those
folders as-is: it supports the helper files Kirikiroid2 uses instead of Windows DLLs. The original
game files are never changed.

Code: `compat/kk4s_compat.cpp` (hooks), `compat/kk4s_plugins.cpp` (plugin replacements).

## Implemented

| File / API | What kk4s does |
|---|---|
| `xp3filter.tjs` in the game folder or in `<data>/profiles/<GameFolder>/` | Executed right before `startup.tjs`. The profile folder keeps a PC game folder unmodified: put the Kirikiroid2 helper files there instead. |
| `Storages.setXP3ArchiveExtractionFilter(func)` | Registers `func(hash, offset, buf, len)`, called for every block read from an XP3 archive (after decompression). `buf` supports `buf[i]` (get/set/`^=`), `buf.xor(start, len, value)` (native, fast), `buf.ptr`, `buf.count`. `void` removes the filter. |
| `Override2.tjs` | Executed right after the game's `Override.tjs` (or after `MainWindow.tjs` if there is none). |
| `AfterInit2.tjs` | Executed after the game's `AfterInit.tjs` (or after `Initialize.tjs` / `startup.tjs`). |
| `Plugins.link("x.dll")` | Built-in replacement if there is one ([plugins.md](plugins.md)), otherwise ignored with `compat: plugin not available, ignored: x.dll`. |

Each helper script runs at most once; errors in it are logged and do not stop the game. All three are
looked up in the game folder first, then in `<data>/profiles/<GameFolder>/` (case-insensitive).

## Engine options from the exe

Windows games carry engine options inside their exe (krkrconf, after `XOPT_EMBED_AREA_`). kk4s reads
them from the biggest exe that has that area and logs them (`compat: Fate.exe options: ...`). Used:
`-evalcontext=global`, and also when the exe predates that option (Kirikiri 2 before ~2.28, e.g. FSN
on 2.19): the `!` eval operator then runs on the global object as it did there. KAG runs `[eval]` as
`exp!` inside `kag`, so `[eval exp="var tx=..."]` otherwise made `kag.tx` and FSN stopped at its
title; Kirikiroid2 packs work around it with `AfterInit2.tjs`, which is no longer needed here.

## Threads

TJS is not thread-safe, but audio decoders read archive files on their own threads. While a script
filter is set, every archive file opened on the main thread (sounds are opened from scripts, so
on the main thread) is read and decrypted completely at once (`XP3Archive.cpp`, files up to 64 MiB);
decoder threads then read memory. If the filter is ever called on another thread, the log says
`compat: xp3 filter script called on a background thread (unsafe)`.

## Not supported yet

- `cxdec.tpm` and other `.tpm` files (Windows decryption plugins) are ignored. A built-in x86
  interpreter is planned so encrypted games work without `xp3filter.tjs`. Until then the log says
  `compat: cxdec.tpm found but no xp3filter.tjs ...` and the game stops with "Cannot convert given
  narrow string to wide string" (encrypted `startup.tjs`).

## Tested

Fate/stay night (Realta Nua), Sharin no Kuni, Tsukihime + Plus-Disc, The Makings of a Lady — all
with Russian fan translations, see [status.md](status.md).

Rule: never commit scripts or data from real games. Tests use synthetic data (`tests/`).
