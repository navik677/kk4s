# Native PackinOne support — where it stopped (2026-11-01)

Goal: run the original `ATRI -My Dear Moments-` repack folder on kk4s without any
extraction/repacking. The work is in the local branch `wip/packinone-native`
(commit `3201fa9`), based on `main` `4d767de`. It is **not** on `main` and not
pushed.

## What the branch implements

`engine/source/core/base/XP3Archive.{h,cpp}`:

1. Parses top-level **`hnfn`** chunks of the XP3 index (4-byte key, 16-bit name
   length, UTF-16LE name) and assigns the real names to the `File` entries by
   `adlr`. Entries whose key has no name are decoys and are dropped (the fake
   `startup.tjs`). `data.xp3` then reports **2302 real names** instead of 2303
   hashed ones.
2. A per-file XOR decryptor (`Kk4sDeriveKey`, `Kk4sMagicXor` in
   `tTVPXP3ArchiveStream::Read`): derives one/two keys from a known prefix per
   extension (PNG, OGG/OPUS, TLG5/6, TJS2100, `FE FE` scramble, `UKR.5`, PSB,
   AJPM, TVP pre-, OTTO, ...), including the classic "first byte uses a
   different key than the rest" variant. Unknown formats are left untouched.
   Diagnostics: `compat: xor <name> k1=.. k2=..` and `compat: xor no magic ...`
   (first 30 files each).
3. `compat::StartupScriptShim()` (`compat/kk4s_compat.{h,cpp}` +
   `ScriptMgnIntf.cpp`): for folders that `kk4s::detail::DetectGameIssue()`
   flags as Steam/PackinOne **and** that have `system/Initialize.tjs`, run a TJS
   source shim instead of the game's `startup.tjs`:

   ```
   global.kirikiriz = true;
   global.debugWindowEnabled = false;
   global.inXP3archivePacked = false;
   global.convertMode = 0;
   System.inform = function(m) { Debug.message("kk4s inform: " + m); };
   System.terminate = function(c) { Debug.message("kk4s: ignored System.terminate(" + c + ")"); };
   Scripts.execStorage("system/Initialize.tjs");
   ```

   This is needed because the real `startup.tjs` is **obfuscated bytecode** (see
   below) and the current TJS2 loader segfaults on it.

`System.addFont` (KiriKiri Z) is already on `main` (`4d767de`), not in this
branch (the branch is based on it).

## Where it stops: the content cipher is a CXDEC keystream

The first-block magic-derived XOR is correct, but the key **changes inside the
file**. Ground truth came from the plaintext written by the game's own
`version.dll` (KirikiriTools) extraction (the file's `adler32` matches the
`adlr` field, so it is byte-exact). Key = cipher byte XOR plaintext byte:

| file | size | key runs (offset → key) |
|---|---|---|
| `system/Initialize.tjs` | 26644 | `0..1131: 0x66`, `1132..: 0xE6` |
| `bgm/BGM01.ogg` | 1747212 | `0..1256: 0xFC`, `1257..: 0x97`; single-byte exceptions at 34575 (`0x0B`), 40087 (`0x98`) |
| `image/bg001.png` (`bgimage.xp3`) | 1453106 | byte 0: `0xBA`, `1..1132: 0x01`, `1133..: 0x83` with single-byte exceptions (7811 `0x42`, 37313 `0x9D`, ...) |
| `steam.tjs` | 19596 | `0..1033: 0x15`, `1034..: 0x28` |
| `アトリ/tatr01_334.tlg` | 2256976 | constant `0x5C` (some TLG are simple: `tatr01_511.tlg` is byte0 `0x11`, rest `0x01`) |
| `AppConfig.tjs` | 1087 | constant `0xC0` |

The switch positions do **not** match `adlr % size`, `adlr & 0xfff`, `time`,
`size & 0xfff`, or a fixed block size. The long constant runs + single-byte
exceptions look exactly like a **CXDEC-generated filter** (format/offset
dependent code, like `FateCrypt`'s special offsets), not like the simple XP3
"first byte / derived byte" scheme. After the first block the TJS2 loader reads
a bogus count and throws `cannot create std::vector larger than max_size()`
(from `tTJSScriptBlock`/`tjsByteCodeLoader`).

### The real `startup.tjs` is also obfuscated bytecode

`tools/tjsdis.py` decodes the first 1300 bytes (decrypted with `0x60`) up to
object 3; object 4's **code words are XORed with 0x61** (object 2, a getter of
the same shape, is plain). The bytecode loader has no support for that
obfuscation and crashes in `tTJSByteCodeLoader::ReadObjects` at
`tjsByteCodeLoader.cpp:222` (gdb backtrace below). This is why the branch uses
the startup shim. It also means a native path must either implement the
bytecode deobfuscation or keep a KiriKiri Z startup shim.

## Suggested next steps (native)

1. Implement the **CXDEC filter**. GARbro's
   `ArcFormats/KiriKiri/KiriKiriCx.cs` (`CxEncryption`) is the blueprint; it
   needs a **control block** (mask, offset, prolog/branch permutations, seeds).
   For this game there is no `.tpm` file; the filter is in
   `ATRI -My Dear Moments-.exe` (the repack's `plugin/PackinOne.dll` registers a
   proxy storage but the content filter is native to the EXE). Candidates for
   extracting the control block:
   - static analysis of the EXE (the `cxdec-hxv4-static-analysis` /
     `KrkrExtractForCxdecV2Extra` projects extract `hxv4_key`, nonce and
     `hash_key` from the EXE without running it);
   - or run the EXE under a debugger and dump the filter/control block after
     it initializes (the filter is likely code-generated in memory).
2. Feed the control block to a C++ port of `CxEncryption.Decrypt(entry, offset,
   buffer, pos, count)`; in `tTVPXP3ArchiveStream::Read` this replaces the
   magic-XOR block. `entry.Hash` is `adlr`; the engine already passes it to
   `TVPXP3ArchiveExtractionFilter`, so the same data is available.
3. Alternatively (only if CXDEC proves too costly): provide a **generic
   per-game key table** derived from a one-time extraction. That is not
   "native" and needs game data; not recommended.
4. Keep the `hnfn` name mapping and the KiriKiri Z startup shim; they are
   independent of the cipher and already tested (the archive lists 2302 real
   names and loads the shim).

## Regression risk: FSN's Russian patch (do not break it again)

On 2026-11-01 the Russian translation of FSN broke while experimenting for ATRI. Cause: the project
dir was changed to the game **folder** when more than one `*.xp3` exists (and, separately, all
`*.xp3` were mounted up front). FSN's translation is `rufix.xp3` + `rupatch.xp3` next to `data.xp3`;
their files override the base ones **only** because they are mounted after `data.xp3` with the
project dir `.../FSN/data.xp3>`. Changing the order/precedence makes the English base files win.

Therefore, when working on the native PackinOne branch:
- keep the WIP gated: the `hnfn` name mapping and the magic XOR must only run for archives that
  actually contain `hnfn` records; the KiriKiri Z startup shim only when `DetectGameIssue()` finds
  Steam/PackinOne markers (`steam_api.dll` + `ds.ini`/`SteamEmu`/`PackinOne.dll`/`*.xp3.index`).
  FSN has none of these, so none of the new code may run for FSN;
- do **not** change `ResolveProjectDir`, the global mount order, or mount all `*.xp3` for all games;
- after merging/testing the branch, launch FSN and check that the log still says
  `project dir: .../FSN/data.xp3>` and that the title/menu and first scenario line are Russian.
  If it is English, the archive precedence changed — revert that part first.

## How to reproduce the analysis

- Game folder: `E:\ATRI -My Dear Moments-` (repack) on the dev PC.
- Plaintext ground truth: `E:\atri-play` (extracted; 10023 files, 3.16 GiB;
  see `docs/repacks.md` for how it was produced).
- Index/name parsing and key-run analysis: scripts in
  `%TEMP%\atri_*.py` (index parser in `atri_keyruns.py`, spike analysis in
  `atri_spikes.py`); `atri-play` files + the archive segments give `key[i] =
  cipher[i] ^ plain[i]`.
- `adler32(plain) == adlr` was verified for `bgm/BGM01.ogg` and
  `system/Initialize.tjs` (`0x69c3ed88`, `0x5df78513`), so the extraction is
  byte-exact.
- gdb backtrace of the startup crash:
  `C:\msys64\ucrt64\bin\gdb.exe -batch -ex run -ex "bt 25" --args
  out-desktop/kk4s.exe "E:\ATRI -My Dear Moments-"`, with
  `KK4S_ROOT=E:\kk4s-pc` in the environment. Crash:
  `TJS::tTJSByteCodeLoader::ReadObjects` at `tjsByteCodeLoader.cpp:222`
  (reading code words past the buffer).

## Build/test notes

- Desktop build: in MSYS2 UCRT64:
  `MSYSTEM=UCRT64 PATH=/ucrt64/bin:/usr/bin:$PATH bash tools/build-desktop.sh`.
  An incremental `cmake --build build-desktop` does **not** copy to
  `out-desktop/`; copy `build-desktop/kk4s.exe` yourself.
- Run a game folder with spaces in PowerShell:
  `Start-Process out-desktop\kk4s.exe -ArgumentList '"E:\ATRI -My Dear Moments-"'`.
- The engine writes logs to `<KK4S_ROOT>/logs/`; screenshots from
  `KK4S_INPUT_SCRIPT` are black with the Direct3D renderer (the game renders
  fine; use a whole-screen capture for visual checks).
- The Switch NRO currently on the SD (`6D75B284…A608383`) was built from
  `main` (`4d767de`, with `System.addFont`) and does **not** contain the WIP
  native branch.
- `out-desktop/kk4s.exe` on the dev PC is the last **WIP** build (magic XOR +
  shim). Rebuild from `main` if you want the extraction-path behaviour, or
  from `wip/packinone-native` to continue the native work.
