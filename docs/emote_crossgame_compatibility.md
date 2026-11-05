# E-mote 3.0 PSB — cross-game compatibility investigation

Branch: `research/emote-crossgame` (created from `research/emote-psb` at
`70ef869`).  NekoPara Vol.1 remains the only game in this environment with
E-mote binaries and PSB resources, so **no second game was empirically
verified**.  This report records what was audited, what the binaries prove, and
exactly which artifacts are missing.

Tools (read-only, no game data committed):

```sh
python3 research/emote-crossgame/audit_dll.py FILE.dll ...
python3 research/emote-crossgame/scan_environment.py [--root DIR]
python3 research/emote-crossgame/inspect_sample.py FILE.psb
python3 research/emote-crossgame/inspect_sample.py --xp3 A.xp3 --entry 1
python3 research/emote-crossgame/test_crossgame_tools.py -v
```

---

## 1. Games actually investigated

| Game | Local path | Engine / E-mote artifacts | Depth of investigation |
|---|---|---|---|
| NekoPara Vol.1 | `game/NekoPara Vol.1/` | Kirikiri2 x86; `plugin/emoteplayer.dll` (1,142,784 B), `emotedriver.dll` (574,464 B); 3 PSB archives | Full: all 157 PSB, DLL constants, filter, Adler, round-trip |
| ATRI -My Dear Moments- | `game/ATRI -My Dear Moments-/` | Kirikiri; **no** `emoteplayer.dll` / `emotedriver.dll`; generic `plugin/psbfile.dll` (197,120 B) | DLL constants + archive index; no E-mote evidence |
| Fate/stay night | `game/fsn/` | Kirikiri; no E-mote plugin | Hashes/constants only |

`scan_environment.py` found **74 PE binaries, 32 XP3 archives, 0 loose PSB
files** under `game/`.  The only binaries containing the NekoPara key string are
`emoteplayer.dll` and `emotedriver.dll`; the only PSB payloads available are the
157 inside the three NekoPara archives.

Other games named by external sources were **not** tested (no files here):
NekoPara Vol.0/2/3/4/Extra, Maitetsu: Last Run!!, KukkoroDays, NinNinDays,
NekoMiko, Sakura: The Jirai-kei Magical Girl, Malus Code, Song of Memories,
Go! Go! Nippon!.  SteamDB's E-mote technology page lists 61 Steam products; TV Tropes lists
E-mote titles.  Those listings prove that other E-mote games exist, **not** that
their PSB files use this key.

## 2. E-mote versions found

| Artifact | Version evidence |
|---|---|
| `emoteplayer.dll` | VS_VERSIONINFO: `FileVersion 1,0,0,1`, `ProductName M2 CheeseWare Motion Library`, `CompanyName M2 Co.,Ltd.`, `OriginalFilename motionplayer.dll`; PDB path `...\branches\emote_development_35\...\emoteplayer.pdb` |
| `emotedriver.dll` | No version resource; PDB/company strings only |
| NekoPara PSB | PSB container version **2**, `encrypt = 0` |
| ATRI `psbfile.dll` | No version resource; exports/strings `PSBFile`, `PSBValueClass`, "invalid psb file" |

The binaries **do not expose an E-mote semantic version**.  "E-mote 3.0" comes
from the earlier project documentation, not from a `3.0` string in these files;
the only build hint is the internal branch name `emote_development_35`.  Any
cross-game version matrix must therefore be keyed by **game build + DLL
SHA-256**, not by a version string read out of the DLL.

## 3. DLL analysis

### 3.1 NekoPara Vol.1

| Item | `plugin/emoteplayer.dll` | `emotedriver.dll` |
|---|---|---|
| SHA-256 | `536361228be24fb47de73e7c72409060577e8b926fefa24e1ffd5854e6779541` | `a3b693b605d67812e489b1fb62cd341012b5514fc9114a032fee4685e70b3a86` |
| Size | 1,142,784 | 574,464 |
| Machine / image base | PE32 i386 / `0x10000000` | PE32 i386 / `0x10000000` |
| Key string `742877301` | file `0xD3794`, VA `0x100D4394`, `.rdata` | file `0x70350`, VA `0x10071350`, `.rdata` |
| Key as u32 immediate `0x2C46C275` | **absent** | **absent** |
| Code references to the key string | 1 (`push imm32` at VA `0x1004BF6D`) | 2 |
| Seeds (`123456789`, `362436069`, `521288629`) | present as u32 immediates in `.text` | present |
| Other markers | `PSB\0`, `emoteplayer`, `motionplayer` | `emotedriver` |

The NekoPara chain is now pinned to concrete instructions:

```
1004bf6d  68 94 43 0d 10       push 0x100d4394        ; "742877301"
1004bf79  e8 8b b3 06 00       call ...               ; parse decimal key
1004bf7e  89 85 44 ff ff ff    mov [ebp-0xBC], eax    ; state.w = key
1004bf84  33 c0                xor eax, eax
1004bf86  c7 85 34 ff ff ff 0c 7c 0d 10   mov [ebp-0xCC], 0x100d7c0c  ; vtable
1004bf90  c7 85 38 ff ff ff 15 cd 5b 07   mov [ebp-0xC8], 0x075BCD15  ; seed x
1004bf9a  c7 85 3c ff ff ff e5 55 9a 15   mov [ebp-0xC4], 0x159A55E5  ; seed y
1004bfa4  c7 85 40 ff ff ff b5 3b 12 1f   mov [ebp-0xC0], 0x1F123BB5  ; seed z
```

This shows the key string is **used** (pushed, parsed, stored into the same
state block as the seeds), not merely present.  It also corrects an error in
`docs/emote_psb_research.md`: file offset `0x4B399` holds **seed x**
(`15 cd 5b 07`), not `0x2C46C275`; the key exists only as the ASCII string and
as the value produced from it at run time.  The old document is left untouched
on `research/emote-psb`; this erratum supersedes that one sentence.

`audit_dll.py` reports `key_u32_immediate_present: false` for every local
binary.  Any future claim that the key is an immediate should be rejected
unless that flag turns true.

### 3.2 Other local binaries

| Binary | SHA-256 (short) | Key string | u32 key | Seeds | E-mote markers |
|---|---|---|---|---|---|
| NekoPara `plugin/psbfile.dll` | `1a1fdb2a7565` | no | no | no | `PSB\0`, `psbfile` |
| ATRI `plugin/psbfile.dll` | `fc4fa66cdfbb` | no | no | no | `PSB\0`, `psbfile` |
| ATRI `ATRI -My Dear Moments-.exe` | `4705821e76fc` | no | no | 3 in `.data` | none |
| `fsn/Fate.exe` | `1280d333a7b2` | no | no | no | none |

Both `psbfile.dll` variants are PSB **container readers**, not E-mote cipher
implementations: no key text, no key reference, no seed block.  The three seed
constants in `ATRI.exe` sit together in `.data` with no key and no E-mote
markers; this is consistent with a generic xorshift RNG, not with the PSB
cipher, and is not treated as evidence.

## 4. PSB resources checked per game

| Game | PSB found | PSB verified | Notes |
|---|---:|---:|---|
| NekoPara Vol.1 | 157 | **157** | 79 `emote.xp3`, 39 `dx_`, 39 `dxlow_`; 168 entries total |
| ATRI | 0 confirmed | 0 | Index names are 32-hex hashes; no `.psb` extension; XP3 segment layout is not the NekoPara variant (extractor stops with "segment size mismatch at 0x4636c2") |
| Fate/stay night | 0 | 0 | No PSB-related plugin |

The NekoPara number was re-checked on this branch with
`research/emote-psb/validate_archives.py`: exit 0, 168 entries, 157 PSB, all
decrypted and round-tripped, inputs unchanged (`work/revalidate_157.log`).

ATRI is **not an E-mote title**: it lacks both E-mote DLLs and every cipher
constant.  Its `psbfile.dll` shows that the PSB *container* is shared M2
technology, but no sample was validated because the archive layout differs and
the game ships no loose PSB.  This is reported as missing data, not as a
compatibility result.

## 5. Cipher, TinyFilter, Adler and structure

| Game | Cipher | Region | XP3 TinyFilter | Adler-32 | Structure | Round-trip |
|---|---|---|---|---|---|---|
| NekoPara Vol.1 | xorshift128, key `742877301`, seeds `123456789/362436069/521288629` | `[0x28, off_choff)` in all 157 | 157/157 match `TinyFilterKey(adlr)` | 157/157 | 157/157 | 157/157 byte-identical |
| ATRI | none found | n/a | not reached (extractor gap) | n/a | n/a | n/a |
| fsn | none found | n/a | n/a | n/a | n/a | n/a |

New tool check on real NekoPara data:

* loose `dx_` sample: structure, round-trip, outside-region unchanged, trie tight
  — all true;
* entry #1 of `emote.xp3` extracted in memory: PSB v2, 298 names, 555 strings,
  80 resources, filter `magic-xor(0xFC)`, Adler matched, all checks true.

The archive layer and the inner cipher remain separate: `inspect_sample.py`
reports `filter_method` (`none` / `magic-xor` / `tinyfilter`) independently of
the cipher key.

## 6. Compatibility classification

| Game | E-mote version | DLL SHA-256 | PSB v | Cipher / key / seeds | Region | TinyFilter | Adler | Structure | Round-trip | Class |
|---|---|---|---|---|---|---|---|---|---|---|
| NekoPara Vol.1 | not exposed; build `emote_development_35`, PSB v2 | `536361…9541`, `a3b693…b3a86` | 2 | xorshift128 / `742877301` / known | `[0x28, off_choff)` | 157/157 | 157/157 | 157/157 | 157/157 | **A** |
| ATRI | no E-mote DLL | `fc4fa6…e464` (`psbfile.dll` only) | - | none | - | - | - | - | - | **U** (not an E-mote title) |
| Fate/stay night | no E-mote DLL | - | - | none | - | - | - | - | - | **U** (not an E-mote title) |
| NekoPara Vol.0/2/3/4/Extra, Maitetsu, qureate titles, etc. | unknown | unknown | unknown | unknown | unknown | unknown | unknown | unknown | unknown | **U** (no artifacts) |

No game receives **A** or **B** without a real PSB passing the full check.  The
single A is the already-proven NekoPara Vol.1 sample set.

## 7. Proven vs hypothesis

**Proven on this machine**

* NekoPara Vol.1: the exact cipher, key derivation from the ASCII string, seed
  immediates, state layout, region rule and byte-exact stream — 157/157 files.
* The key is referenced from code (`push 0x100D4394`) and stored as the fourth
  state word; it is **not** a `0x2C46C275` immediate.
* XP3 TinyFilter is a separate layer; its formula and Adler-32 hold for all 157
  NekoPara PSB, including `dx_`/`dxlow_`.
* `psbfile.dll` (M2) implements the PSB container without the E-mote cipher;
  the cipher lives in `emoteplayer.dll` / `emotedriver.dll`.
* No other local binary contains the key string, key reference, or E-mote
  markers.

**Hypothesis / unverified**

* The same key is shared by other NekoPara volumes and other E-mote 3.0 games.
  External extraction guides use `742877301` for the NekoPara series, but that
  is not an independent game test.
* Other builds may use a different key, different seeds, a different region
  rule, or a keyed/derived init block.
* PSB v3/v4 and nonzero `encrypt` remain untested on real samples.
* ATRI's `psbfile.dll` reads an unencrypted PSB variant; not verified because no
  sample could be extracted with the current XP3 reader.

## 8. Artifacts needed for the next step

1. One PSB **and** the matching `emoteplayer.dll`/`emotedriver.dll` from a
   different E-mote title (a free demo is enough), with permission to inspect
   them.  Record game/build, file sizes and SHA-256.
2. Preferably the containing XP3 with its index, so `adlr` and the archive
   filter can be checked independently of the cipher.
3. For ATRI-style archives: either loose PSB files or the XP3 segment-layout
   variant, so the container reader can be extended without guessing.
4. If the key differs: the code around the state initialisation (or a
   disassembly window covering the `push` of the key string), so the new key can
   be recovered from evidence rather than brute force.

Do not download pirated game files; a demo, a freely distributed sample, or a
copy supplied by the user is sufficient.

## 9. Recommended kk4s architecture

Keep the layers separate so an unknown build cannot silently produce garbage:

```
XP3 reader ──► archive filter (TinyFilter/None) ──► PSB container parser
                                                        │
                                     ┌──────────────────┴─────────────────┐
                                     ▼                                    ▼
                            cipher profile (build)              PSB value model
                            - seeds, key source                 - names/strings
                            - region rule                       - objects/arrays
                            - refill/byte order                 - resources
                                     │                                    │
                                     ▼                                    ▼
                            decrypted PSB bytes                 animation/timeline adapter
                                                                        │
                                                                        ▼
                                                                 render backend
```

* **Universal:** XP3 index/segment reader, PSB v2–v4 container and value model,
  Adler/filter plumbing, structural validator, error reporting.
* **Profile adapter:** cipher key/seed/region/refill and the PSB→animation
  schema.  Select by a hash-pinned profile (DLL SHA-256 + plugin name + PSB
  version), never by assuming "E-mote 3.0" implies one key.
* **Unknown build:** parse the container, report `unknown cipher profile`, and
  refuse to emit animation data.  A profile must be added only after a real
  sample passes structure + round-trip + Adler.
* **Detection inputs:** `emoteplayer.dll`/`emotedriver.dll` hash, plugin
  filename, PSB version/`encrypt` flag, root keys, presence of `psbfile.dll`
  (container only), and archive filter behaviour.
* **Tests:** one golden sample per profile, hash-pinned, plus negative tests for
  wrong key, unknown version and truncated data.  Keep the existing 35 decoder
  tests and 8 tool tests green.

No renderer, animation runtime or `cloud-session` integration is implemented
here; this is only the proposed boundary.

## Appendix A — raw audit evidence (2026-11-05)

All commands are read-only; outputs are under
`research/emote-crossgame/work/` (gitignored).

```sh
python3 research/emote-crossgame/audit_dll.py \
  "game/NekoPara Vol.1/plugin/emoteplayer.dll" \
  "game/NekoPara Vol.1/emotedriver.dll" \
  "game/NekoPara Vol.1/plugin/psbfile.dll" \
  "game/ATRI -My Dear Moments-/plugin/psbfile.dll" \
  "game/ATRI -My Dear Moments-/ATRI -My Dear Moments-.exe" \
  "game/fsn/Fate.exe" --json-out research/emote-crossgame/work/audit_dll.json
python3 research/emote-crossgame/scan_environment.py
python3 research/emote-crossgame/inspect_sample.py \
  --xp3 "game/NekoPara Vol.1/emote.xp3" --entry 1
```

Exact locations behind the summary tables:

| Fact | Value |
|---|---|
| player key string | file `0xD3794`, VA `0x100D4394`, `.rdata` |
| player key `push imm32` | file `0x4B36D`, VA `0x1004BF6D` |
| player init block | file `0x4B393`, VA `0x1004BF93`; writes vtable `0x100D7C0C` at `[ebp-0xCC]`, seeds at `[ebp-0xC8/-0xC4/-0xC0]`, `[ebp-0xBC]` = parsed key |
| player seed immediates | file `0x4B399/0x4B3A3/0x4B3AD`, VA `0x1004BF99/0x1004BFA3/0x1004BFAD` |
| driver key refs | file `0xD50` → VA `0x10001950`; file `0x2069` → VA `0x10002C69` |
| ATRI exe seed block | `.data` file `0x3AE550/0x3AE554/0x3AE558`, no key string |
| environment scan | 74 PE files, 32 XP3, 0 loose PSB under `game/` |
| XP3 entry #1 (`emote.xp3`) | 298 names, 555 strings, 80 resources, region `0x28..0x3925D`, filter `0xFC`, all checks true |
| loose `dx_` sample #1 | 467 names, 491 strings, 3 resources, region `0x28..0x3E2BB`, filter `0x56`, all checks true |
| archive revalidation | 168 entries, 157 PSB, 157 decrypted/round-trip/adler, 0 errors, inputs unchanged |

`audit_dll.py` reports `key_u32_immediate_present: false` for all six audited
binaries, including both NekoPara DLLs.  The old sentence in
`docs/emote_psb_research.md` that called `0x4B399` the key is corrected there.

## Appendix B — external sources and what they do (not) prove

| Source | What it establishes |
|---|---|
| <https://steamdb.info/tech/SDK/E-mote> | SteamDB auto-detects E-mote in 61 Steam products; a population, not a key test |
| <https://tvtropes.org/pmwiki/pmwiki.php/MediaNotes/Live2D> | E-mote works include Go! Go! Nippon!, Malus Code, NekoMiko, Nekopara, Song of Memories |
| <https://gist.github.com/LukeZGD/64b0ecfb7eaccdfbb327ddfea4621723> | NekoPara extraction guide; `EmtConvert … 742877301` for the NekoPara series — same-series evidence, not an independent game |
| <https://store.steampowered.com/app/3009640/> | Sakura, The Jirai-kei Magical Girl is advertised as E-mote-powered |
| <https://www.gonintendo.com/archives/341492> | KukkoroDays uses E-mote |
| <https://tyranoscript.com/> | E-mote can be used outside Kirikiri (TyranoScript plugin), so engine alone does not identify a build |
| <https://github.com/storycraft/emote-psb-rs> | MIT-licensed description of PSB/MDF; read-only reference, no code copied |

None of these sources contains a second game's PSB plus DLL, so none can move
that game out of class U.  They are listed to make the gap auditable, not to
claim compatibility.

## 10. Git state and reproducibility

* Branch: `research/emote-crossgame`, based on `research/emote-psb@70ef869`.
* Added: `research/emote-crossgame/{audit_dll,scan_environment,inspect_sample,test_crossgame_tools}.py`,
  `README.md`, this report, and a `.gitignore` entry for
  `/research/emote-crossgame/work/`.
* Decoder and its tests were **not modified**; `research/emote-psb` is used
  read-only.
* Empirical outputs: `work/audit_dll.json`, `work/env_scan.json`,
  `work/inspect_sample.json`, `work/revalidate_157.log` (gitignored).
* Cross-game tests: 8, all passing.  NekoPara decoder tests: 35, all passing.
* Cross-game audit commit: `cbdb190`; documentation update: `85b3893`.
* Push: `origin/research/emote-crossgame` updated successfully.
* No game DLL, PSB, XP3 or extracted resource is part of any commit.
