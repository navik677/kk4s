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

## Encrypted archives without any helper file

Many games ship a `.tpm` plugin whose decryption is a small keyless function of each entry's
`adlr` hash and offset. kk4s recognises these without an `xp3filter.tjs`
(`compat/kk4s_xp3crypt.cpp`): when an archive with plain names has no script filter, a few small
entries are checked; if they do not match their `adlr` as stored, the known families are tried
(FlyingShine, Seiten, Okiba, Dieselmine, Damegane, Nephrite, Exa, HighRunning, Hibiki, PinPoint,
PuCa, TinyFilter) and then "one XOR byte per entry": the byte of each sample is recovered against its
adlr and a formula of the adlr is fitted (a constant; `((adlr >> s) + a) ^ c`; `(adlr >> s1) ^
(adlr >> s2) ^ c`; optionally the first 5 bytes left plain). A scheme is bound only when it explains
(nearly) all samples. The log says `xp3crypt: <archive> uses <scheme>`, or that the archive looks
encrypted but no simple scheme fits. Schemes that need a table or secret constant of one particular
game are not included (no game keys in the repository); those still need an `xp3filter.tjs` or a
CX scheme. Schemes whose `adlr` is computed after encryption cannot be verified this way and are
not detected. Plain archives cost three small reads. Tested by `tests/xp3/crypt_smoke.py`.

**The game's own `.tpm`, run in an x86 interpreter** (`compat/kk4s_tpm.cpp`, `compat/x86/`). Before the
keyless families, kk4s loads every `*.tpm` in the game folder and `plugin/` into a small IA-32
interpreter (integer instructions, no SSE / x87 maths, a fake `iTVPFunctionExporter` for both the
stdcall and the cdecl tp_stub generations, a minimal fake Win32 API), runs `DllMain` and `V2Link`, and
captures the callback the plugin passes to `TVPSetXP3ArchiveExtractionFilter`. That filter is bound
only when it makes (nearly) all sample entries match their `adlr`; the key never leaves the game's
file. If the filter turns out to be a XOR whose keystream repeats with a short period after the first
2 KiB (checked on the samples and at offsets up to 2 GiB), each file's 4 KiB keystream prefix is computed
once by the interpreter and the rest is native; otherwise every read runs in the interpreter (slow:
about 35 million guest instructions per second on the dev PC, the Switch is unmeasured). The log says
`tpm: <plugin> sets an XP3 filter (...)`, `tpm: <archive> uses <plugin> (...)`, or why a plugin was not
used (`V2Link did not set an XP3 extraction filter`, `the plugin called <function> (not provided by
kk4s)`, ...). Not handled: plugins that read their key from another file, need threads, SEH, SSE or
x87 maths, or check the exe; CXDEC plugins go through the CX path instead. Tested with synthetic
plugins only (`tests/tpm`), never with a real game's `.tpm`.

## Not supported yet

- Running a Windows `.tpm` plugin is not supported; its decryption is reimplemented natively instead.
  `compat/cxdec` has the Cx program interpreter and the scheme-free "TinyFilter" variant (see
  [cxdec.md](cxdec.md)). A filter is bound to an archive only after that archive's own entries
  validate (`adler32` of the decrypted data equals `adlr`). On desktop a Cx scheme is recovered
  automatically from the game's TPM or its embedded module; on Switch a Cx game still needs
  `<data>/profiles/cx/<name>.cxscheme`, while the TinyFilter variant needs no data at all. A game
  whose `startup.tjs` no filter explains still stops with "Cannot convert given narrow string to
  wide string".

## Tested

Fate/stay night (Realta Nua), Sharin no Kuni, Tsukihime + Plus-Disc, The Makings of a Lady — all
with Russian fan translations, see [status.md](status.md).

Rule: never commit scripts or data from real games. Tests use synthetic data (`tests/`).
