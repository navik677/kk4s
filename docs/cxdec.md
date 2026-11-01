# CXDEC (KiriKiri "Cx" XP3 filter) — findings and architecture

Status (2026-11-01, branch `wip/packinone-native`): the algorithm is identified and a
reference decoder is verified on a real game (ATRI -My Dear Moments-, PackinOne repack).
**Not integrated into the runtime yet.** The open problem is how kk4s obtains a game's
parameters automatically (layer 2/3 below).

## Layer 1 — generic CXDEC (shared by every Cx game)

Same as GARbro `CxEncryption` (`ArcFormats/KiriKiri/KiriKiriCx.cs`). Reference port:
`tools/cxdec_ref.py` (no game data inside).

- Per file key = `adlr` (adler32 of the plaintext; this also gives a free integrity check).
- Two regions: `base = (adlr & mask) + offset`. Bytes `< base` use key `h`, bytes
  `>= base` use `h ^ (h >> 16)`.
- `Decode(key)`: seed = `key & 0x7f` selects one of 128 generated programs; `ret1 =
  P(key >> 7)`, `ret2 = P(~(key >> 7))`. Every byte of the region is XORed with
  `key3 = ret1 & 0xff` (0 → 1); two single bytes get an extra XOR: position
  `ret2 >> 16` with `ret1 >> 8`, position `ret2 & 0xffff` (+1 if equal) with
  `ret1 >> 16` (positions are absolute file offsets, so only the first 64 KiB).
  This is exactly the "constant run, switch at ~1000, single-byte exceptions" pattern
  measured earlier — the observed XOR values are outputs, not keys.
- Program generator: LCG `s = 1103515245*s + 12345`, `r = s ^ (old<<16) ^ (old>>16)`;
  recursive body with stages 5..1, 128-byte length limit; the RNG is **not** reset
  between stages. The ordering matters: the opcode is checked first and the random
  immediate is drawn only after the opcode fitted (`Emit(op) && EmitRandom()`); a port
  that draws first produces wrong programs for the seeds that overflow (here 114/125).
- Control block: 1024 little-endian uint32, read directly by the generated code
  (`mov eax,[esi+eax*4]`). GARbro stores `~raw` and executes `~ControlBlock[i]`, i.e.
  the raw values.

## Layer 2 — per-game scheme (data, not code)

A scheme is `{mask, offset, PrologOrder[3], OddBranchOrder[6], EvenBranchOrder[8],
control_block[4096]}` — the same fields as GARbro's `CxScheme`. Variants seen in GARbro
(Senren/Cabbage/Nana/Riddle) change the PRNG (`CxProgramNana`) or the name table, not
the decode math; they fit as flags on the same scheme.

ATRI values: `mask 0x1A7`, `offset 0x369`, `prolog [0,1,2]`, `odd [0,4,3,1,2,5]`,
`even [2,7,4,0,3,5,1,6]`, control block sha256 `76b332a6…5d70c821`. Not committed (it is
game data); it was taken from the running game for verification only.

How they were obtained (ATRI):
1. The game's process holds a **manually mapped, unlisted PE image** at `0x10000000`
   (private memory; not in the module list) containing the Cx generator (LCG
   constants, `and 0x1A7`, `add 0x369`) and the control block at `0x10014628`.
2. A 128 × 128-byte RWX region holds the generated programs (prolog
   `57 56 53 51 52 8B 7C 24 18`); `mov esi, imm32` in them gives the control block
   address.
3. The branch orders were solved by regenerating programs and matching the bytes:
   with the exact emit order, **100/100** dumped programs are reproduced.

Verification: decrypting **every** entry of `data/bgimage/fgimage/voice/vol1/steam.xp3`
gives `adler32 == adlr` for 10 808 entries; all 128 seeds of both keys are covered.
Exceptions: one decoy entry per archive (`$$$ This is a protected archive. $$$ …txt`,
not real content) and `main/imagedressmap.txt` (zlib error while reading — to check).

## Layer 3 — detection and initialization (open)

- Detection is easy and safe: an XP3 index with `hnfn` records (Cx name table) → the
  archive is Cx-encrypted; `adlr` lets kk4s verify any candidate scheme on the first
  file it opens before enabling it. Archives without `hnfn` are not touched (FSN).
- **Blocker: the scheme is not stored in plain form on disk.** Neither the control
  block, nor the hidden module's code/rdata, nor its constants appear in the EXE, the
  plugins, `.cf`, `.sig` or `.index` files (also not XOR/NOT-ed). The hidden image is
  unpacked at runtime by the EXE or `PackinOne.dll` (EXE `.rsrc` has entropy 7.17 —
  first candidate). Until that unpacking is understood, kk4s on Switch has no way to
  get the parameters from the original folder by itself.

Options, in order of preference:
1. Reverse the unpacker (EXE/`PackinOne.dll` → hidden image) and extract the scheme
   statically from the original files on first launch. Fully automatic, works on
   Switch. Unknown effort; the unpacker may be shared by other PackinOne/Steam KiriKiri
   titles, so it is reusable infrastructure.
2. Read the scheme from the EXE when it *is* in plain form (older Cx games / TPM
   plugins: GARbro's `" Encryption control block"` signature scan). Generic, cheap,
   but does not cover ATRI.
3. One-time scheme capture on a PC (run the game, dump as above) stored as a small
   scheme file next to the game. Works for any Cx game, but is a preparation step.

## Runtime integration plan (after the blocker)

- `CxFilter` (pure C++, no platform code): scheme struct, generator + interpreter
  (no JIT; portable to Switch/Linux/Windows), 128 programs cached per archive set.
- Hook in `tTVPXP3ArchiveStream::Read` where the WIP magic-XOR is now, enabled only
  for archives flagged by `hnfn` **and** a scheme that passes the `adlr` check;
  remove the magic-XOR guesser.
- Regression gate: FSN Russian patch (`rufix.xp3`/`rupatch.xp3`, no `hnfn`) must take
  none of the new paths; compare against `4d767de`.
