# CXDEC (KiriKiri "Cx" XP3 filter)

Status (2026-11-01, branch `wip/packinone-native`, not on `main`):

- Generic decoder in the runtime (`compat/cxdec`), verified byte-exact.
- Scheme recovery **without running the game** works for ATRI (PC tool).
- The runtime decrypts the original ATRI folder transparently; the game renders its
  first screens (not an encryption problem). See "ATRI status".
- FSN Russian patch: no change against `main` `4d767de` (title screen, logs).

## Three layers

| layer | what | where | game data? |
|---|---|---|---|
| 1 generic | generator + interpreter + decrypt | `compat/cxdec/cxdec.{h,cpp}` | none |
| 2 per game | a *scheme*: mask, offset, 3 branch orders, 4 KiB control block | `<root>/profiles/cx/<name>.cxscheme` + `.cb` (user data, never in the repo) | yes |
| 3 detection | which archive is Cx, which scheme fits | `compat/kk4s_cx.cpp`, hook in `XP3Archive` | none |

### Layer 1 — algorithm (shared by every Cx game)

Same as GARbro `CxEncryption` (`ArcFormats/KiriKiri/KiriKiriCx.cs`).

- Key = entry `adlr` (adler32 of the plaintext — also a free integrity check).
- Two regions: bytes `< (adlr & mask) + offset` use key `h`, the rest use
  `h ^ (h >> 16)`.
- Per region: program `P = programs[key & 0x7f]`, `r1 = P(key >> 7)`,
  `r2 = P(~(key >> 7))`. Every byte is XORed with `r1 & 0xff` (0 → 1). Two single
  bytes get an extra XOR: the one at file offset `r2 >> 16` gets `r1 >> 8`, and the one at
  `r2 & 0xffff` (+1 if the two offsets are equal) gets `r1 >> 16`.
- Program generator: LCG `s' = 1103515245*s + 12345`, `r = s' ^ (s<<16) ^ (s>>16)`;
  recursive body, stages 5..1, 128-byte x86 length limit, the RNG is not reset between
  stages. **Order matters**: an opcode is length-checked first and its random
  immediate is drawn only afterwards (`Emit(op) && EmitRandom()`). Drawing first
  produces wrong programs for the seeds that overflow (ATRI: 114, 125).
- Control block: 1024 LE uint32 used raw (`mov eax,[esi+eax*4]`).
- kk4s interprets the programs (no JIT), so it is the same code on Switch, Linux and Windows.

Tests: `tests/cxdec/cxdec_test.cpp` — 384 synthetic vectors from the Python reference
(`tools/cxdec_ref.py`, `tests/cxdec/gen_vectors.py`), incl. chunked reads; plus
`--archive` mode for a real game (adler32 of every entry).

### Layer 2 — where schemes come from

The Cx filter lives in a **TPM plugin** (`cxdec_tpm.dll`/`*.tpm`, a KiriKiri plugin
exporting `V2Link`). It contains the generator code, `and r, mask; add r, offset`, the
branch switches and the control block.

How the TPM is shipped:

1. Classic: a `*.tpm` file next to the EXE. The control block may start with GARbro's
   `" Encryption control block"` signature.
2. **Embedded "internal module"** (ATRI): the EXE holds
   `"internal module\0"` + 8 bytes + `u32 raw_size` + `u32 zlib_size` + zlib data;
   it inflates to the TPM DLL, which the EXE maps by hand at run time (private memory,
   not in the module list — that is why nothing was visible on disk at first).
   This is a container format, not ATRI data; it is parsed generically.

`tools/cx_scheme_extract.py GAME_DIR OUT.cxscheme` (PC, needs `capstone`, `pefile`):

- finds TPMs (files or internal modules that contain the LCG constant);
- control block candidates: the signature, or 4-KiB data blocks returned by tiny
  getters (`mov eax, imm32; ret`);
- mask/offset candidates: `and r32, imm32; add r32, imm32` on one register;
- branch orders: the `div 6` / `and 7` jump tables and the `div 3` compare chain of
  the generator; each case is classified by the x86 bytes it emits
  (`01 D8` add, `29 D8` sub, `0F AF C3` imul, `B8` mov imm, `BE` control block …);
- every candidate is **validated on real entries** (decrypted adler32 == adlr); the
  best one that explains ≥80 % of the samples wins (protectors add decoy entries).

Result for ATRI: the scheme from the original folder alone is identical to the one
captured from the running game (control block byte-identical, 100/100 generated
programs reproduced). No ATRI constant is in the tool.

### Layer 3 — detection in the runtime

- An XP3 index with `hnfn` records ⇒ Cx archive (names come from `hnfn`; entries
  without a name are decoys and are dropped). Other archives are untouched.
- `BindCxDecoder()` tries every `*.cxscheme` on ≤12 small entries and binds the best
  one with ≥80 % verified; otherwise logs how to create a scheme. A scheme is never
  applied unverified.
- The KiriKiri Z startup shim runs only after a verified Cx binding (positive
  detection). The earlier folder-marker gate also matched FSN and broke it.

## When automatic recovery is not possible

| case | what to do |
|---|---|
| Generator variant with another PRNG (GARbro `CxProgramNana`: Nana/Cabbage/Riddle) | add the variant to layer 1 as a scheme flag; the extractor must detect it |
| Compiler shape the extractor's switch parser does not know | extractor reports "could not read all branch orders"; fix the parser or provide a scheme by hand |
| TPM packed/encrypted by something other than the zlib internal-module container | unknown loader ⇒ scheme must be provided (profile) or captured once on a PC |
| Archives with `hnfn` but a non-Cx cipher | no scheme validates ⇒ nothing is applied; needs its own filter |
| Non-Cx TPM (FSN's `cxdec.tpm` is FateCrypt; no generator) | extractor finds no TPM ⇒ correct; such games keep using `xp3filter.tjs` |

Not done yet: running the extractor **inside** kk4s. It needs a small x86 decoder
for the switch parsing (capstone is a PC-only dependency). Until then a scheme is
produced once with the PC tool and copied to `<root>/profiles/cx/`. This is the
remaining manual step for "folder → play" on Switch.

## ATRI status (original folder, desktop build)

- All six Cx archives bind automatically (`12/12` sample entries verified);
  `patch.xp3` (no `hnfn`, a Russian translation by CreativeCorner) loads as usual;
  the window title is Cyrillic.
- C++ decoder over every entry: 10 789 entries with adler32 == adlr; failures are the
  decoy entry per archive and `main/imagedressmap.txt` (its zlib segment does not
  inflate; to investigate).
- Verified on Switch too (original folder on the SD card): all Cx archives bind, `patch.xp3` loads.
- On the desktop build the game now runs the logo/warning scenario and **renders its first screen**
  (WARNING page: background image from a `.pimg`, Japanese text). Fixed on the way (all general):
  `win32dialog.dll` stub, `psbfile.dll` (`PSBFile` + `psb://` media; parser in `compat/psb`, tests in
  `tests/psb`), `Scripts.foreach/getObjectKeys/getObjectCount/clone/equalStruct`, custom storage media
  names (`psb://`, `var://`) that were turned into file paths, the temp directory
  (`sdmc:/tmp` -> `<data>/tmp`), `[emb escape=false]` and the `taglist` element field of
  `KAGParserEx.dll` (emulated in the built-in KAG parser, only for games that link that plugin).
- **Steam chain (fixed 2026-11-01):** kk4s now detects the Steam build (`steam_api.dll` +
  `steam.tjs`/`steam.xp3` in the folder), passes `-steam=yes` and `-debugwin=no` (release semantics)
  and provides `krkrsteam.dll` (a `Steam` object; achievements logged, cloud off). `steam.tjs` then
  installs its `[実績]` handling and the game no longer dies there.
- **It now reaches the title screen** (`title.ks`) from the original folder, with the Russian menu
  rendered. The remaining pieces are the real `textrender.dll` (currently a no-op stub) and the other
  KAGZ plugins (`layerEx*`, `AlphaMovie`, ...). Details in `docs/handoff-native-cx.md`.

## FSN regression check (2026-11-01)

`main` `4d767de` and this branch, same folder, `profiles/Fate-stay_night/xp3filter.tjs`:
identical title screen (Russian), `project dir: …/data.xp3>`, same mount order
(`rupatch.xp3`, `rufix.xp3`), 0 script exceptions, no `cxdec`/shim lines. Logs differ
only in timings and pointers. Not yet re-checked: in-game scenario, save/load,
Switch build.
