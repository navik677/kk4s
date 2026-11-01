# Protected / repacked games (PackinOne, "hashed" XP3)

kk4s runs a plain KiriKiri folder as it is. Some PC releases are wrapped by a
protection layer that is **not** part of KiriKiri: the archive index hides the
file names and the file contents are XOR-encrypted, and a native Windows DLL
serves the real files to the game. ATRI -My Dear Moments- (DARKSiDERS Steam
repack) is the test case.

This page records what the wrapper does and the one-time extraction procedure
we verified. It is a fallback: the extracted folder runs on kk4s, the original
repack folder does not (yet).

## What the ATRI repack does

- `data.xp3` (and bgimage/fgimage/voice/vol1/steam) put fake names into the
  normal XP3 `File` records: `info` holds `md5(lowercase name in UTF-16LE)`,
  not the file name.
- The real names are in **`hnfn` chunks** in the XP3 index: 4-byte key,
  16-bit name length, UTF-16LE name. The key matches the `adlr` value of the
  corresponding `File` record. GARbro supports the same records
  (`FilenameMap` in `ArcXP3.cs`).
- The `startup.tjs` visible by name is a decoy: 316 bytes of TJS2 bytecode
  that only calls `System.inform("startup failed: corrupted execution file")`.
  The real one is one of the hashed entries (1300 bytes, XOR-encrypted).
- Protected contents are a **single-byte XOR**, but not always one key: the
  first byte can use a different key than the rest (the classic XP3
  "simple crypt": first byte = base key low byte, rest = derived byte). The
  keys are per file and are not stored in the index; derive them from a known
  magic (`PNG`, `OggS`, `TLG`, `TJS2100`, `FE FE`, `UKR.5`, ...).
- The native `version.dll` in the repack is **KirikiriTools /
  KirikiriUnencryptedArchive** (arcusmaximus), which lets extractors read
  decrypted streams and writes files to `unencrypted/` as the game opens them.
- `data.xp3.index` is byte-identical to the decompressed index; it is a cache,
  not a key store.

## Extraction procedure (Windows, one-time)

The game folder is never modified; a mirror with hard links is used instead.

1. Mirror the game folder to a work folder (hard links; same volume):
   `ATRI -My Dear Moments-` -> `E:\atri-extract-test`.
2. Put an empty marker `extract-unencrypted.txt` in the work folder.
3. Generate `unencrypted/startup.tjs` (UTF-16LE with BOM) that:
   - adds every `*.xp3` next to the exe to the auto path,
   - loops over the union of names from all `*.xp3.index` files and calls
     `Scripts.evalStorage(name)` in `try/catch` (the DLL writes each opened
     file to `unencrypted/<real name>`),
   - ends with `System.exit()`.
4. Run the game exe from the work folder. It loads the marker script from
   `unencrypted/`, extracts most files and exits.
5. `fgimage.xp3` is different: its names are Japanese directories
   (`アトリ/...`, `キャサリン/...`, ...) and the DLL cannot create them on a
   non-Japanese locale. Extract that archive with a script: parse the `hnfn`
   names + `File` segments, decompress zlib segments, derive the two XOR keys
   from `TLG` and write the files to the right Unicode paths.
6. `patch.xp3` is an ordinary unencrypted XP3 (the Russian translation:
   `Config.tjs` and `*.ks.scn`). Extract it with any XP3 tool or the
   `tools/` helper; overlay it on the extracted tree.
7. Put a `startup.tjs` stub into the tree. The game's own `startup.tjs` is
   KiriKiri Z bytecode and the current TJS2 bytecode loader crashes on it; the
   stub is equivalent and keeps two KAGZ-only gaps from killing the process:

   ```tjs
   global.kirikiriz = true;
   global.debugWindowEnabled = false;
   global.inXP3archivePacked = false;
   global.convertMode = 0;
   System.inform = function(m) { Debug.message("kk4s inform: " + m); };
   System.terminate = function(c) { Debug.message("kk4s: ignored System.terminate(" + c + ")"); };
   Scripts.execStorage("system/Initialize.tjs");
   ```

   `System.inform` would open a blocking message box; the game uses it for a
   missing append disc and a missing font, both harmless. KAGZ's
   `custom.tjs` throws `Member "textSpeechMenuItem" does not exist` (a menu
   entry built with a KiriKiri Z API); KAG's exception handler calls
   `System.terminate`, which would exit. Ignoring it lets the game continue
   (confirmed: the game stays up and renders). Remove the overrides once the
   missing KZ APIs (`SystemConfig`, pad keys, ...) are implemented.

The resulting folder is a normal plain-file project: copy it to
`<SD>/switch/kk4s/games/<name>/` and run it.

## State (2026-11-01)

- Extracted: ~7258 files by the DLL (data/bgimage/voice/vol1/steam) + 2559 TLG
  and 49 PBD from fgimage + 197 from patch.xp3.
- The extracted folder **runs in the PC viewer** (`out-desktop/kk4s.exe`) and
  renders the game (title/prologue scene confirmed). It needed the engine's
  new `System.addFont(face, storage)` (KiriKiri Z API, `SystemIntf.cpp`).
- KAGZ's `custom.tjs` still throws at `reparent(textSpeechMenuItem, ...)`
  (a menu item created by a KZ API kk4s lacks). KAG's exception handler calls
  `System.terminate`; the startup stub above ignores it and the game keeps
  running (confirmed: the process stays up for minutes and renders the
  title/prologue).
- Not yet: runtime decryption of the original repack folder (no extraction).
  That would need `hnfn` names + the magic-derived XOR in `XP3Archive.cpp`.
- A few files are not extracted (28 TLG, 10 `.sinfo`, 7 `.stand`); they are
  metadata/animation pieces, not needed to start.

## Do not

- Do not change `ResolveProjectDir` or the global XP3 mount order; FSN's
  Russian patch depends on it (see `docs/HANDOFF.md`).
- Do not write into the user's game folder: extract into a mirror/copy.
- Do not commit game files, names lists with content, or keys.
