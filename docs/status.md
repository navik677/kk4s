# Status

Last update: 2026-10-31.

## Builds (CI, every push to main)

| Target | State | Where to get it |
|---|---|---|
| Switch `kk4s.nro` (+ `.elf`) | builds, **runs on hardware** | [nightly release](https://github.com/navik677/kk4s/releases/tag/nightly), Actions artifact `kk4s-<sha>` |
| PC viewer, Linux (ASan + UBSan) | builds, smoke test passes (hello game 15 s under xvfb) | Actions → desktop → `kk4s-linux-<sha>` |
| PC viewer, Windows (MinGW) | builds | Actions → desktop → `kk4s-windows-<sha>` (exe + DLLs) |

## Tested games

All unmodified, Russian fan translations, on a real Switch (title takeover) and the Linux PC viewer.

| Game | Notes |
|---|---|
| Fate/stay night (Realta Nua, Kirikiroid2-ready folder) | `xp3filter.tjs` (cxdec-style XOR), `Override2.tjs` / `AfterInit2.tjs`, game font "Mona" with full-width Cyrillic (substituted) |
| Sharin no Kuni, Himawari no Shoujo | plain archives; saving, touch and gamepad checked |
| Tsukihime + Plus-Disc | own font (`umineko.ttf`) |
| The Makings of a Lady – Purity Yours to Defile | `xp3filter.tjs` (`b.xor(0,l,h)`); existing saves from the game folder are migrated |

## Working

- **Launcher** with covers (carousel / grid), VNDB cover search, settings, game options, save
  backups, autostart, 4 UI languages — see [launcher.md](launcher.md).
- **In-game menu** (+ / −): quick save / load, volume, controls, exit to the launcher.
- **Input**: Switch buttons via libnx by label, configurable per button; touch = left mouse button;
  keyboard → Windows VK codes; `System.getKeyState` (skip with Ctrl held).
- **Saves** outside the game folder (`<data>/saves/<game>/`), migrated on first start.
- **Kirikiroid2 conventions**: `xp3filter.tjs`, `Storages.setXP3ArchiveExtractionFilter`,
  `Override2.tjs`, `AfterInit2.tjs` — see [kirikiroid-compat.md](kirikiroid-compat.md).
- **Plugins**: native replacements for common DLLs — see [plugins.md](plugins.md).
- **Fonts**: game-folder and system fonts (Nintendo shared fonts on Switch), fallback for unknown
  names, proportional Cyrillic for Japanese fonts that draw it full-width, face cache.
- **Logs**: one per run, written by a background thread; engine script errors with TJS stack traces.

## Engine fixes (all marked `kk4s:` in `engine/`)

- Relative storage names inside `data.xp3>` were never found (krkrs prefixed every name with `file:/`).
- Case-insensitive file lookup on case-sensitive file systems; when creating folders the existing
  part keeps its real spelling (a lower-cased copy of the whole path was created before).
- TJS xp3 filters ran on the audio thread while the main thread ran scripts; TJS is not thread-safe
  (random crashes). With a script filter set, archive files opened on the main thread are decrypted
  completely there. Non-main threads also have their own register stack and never touch the shared
  stack refcount.
- Closed windows (e.g. the save dialog) keep `TJSNativeInstance == NULL` until deleted: checked in
  input dispatch.
- Font faces were recreated on every font switch (slow on the SD card): cached.
- Touch: event cases fell through (one tap = two clicks); touch → mouse hints had wrong names.
- SDL keycodes are translated to VK codes; `TVPGetKeyMouseAsyncState` works.
- Engine log (`TVPAddLog`) goes to the kk4s log file.
- FFmpeg audio decoder ported to the send/receive API; TLG5 compressor buffer overflow; system
  headers inside `namespace TJS`; missing `return`; MinGW path shims.

## Not done / not checked

- Video (`krmovie.dll` path through FFmpeg) not tested.
- Some plugin replacements are approximations or stubs (extrans transitions fall back to crossfade,
  `firespark` / lens effects missing).
- `.tpm` decryption plugins (e.g. `cxdec.tpm`) are not run: encrypted games need an `xp3filter.tjs`.
- The game picture is stretched to the whole screen (no aspect-correct scaling / letterbox yet).
- Overlay folder and per-game profiles (M2/M3) are not implemented.
- Album (applet) mode memory limits not tested; docked ↔ handheld switching not tested.
- Windows build: 16-bit `wchar_t` differs from Switch/Linux.
