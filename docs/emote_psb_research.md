# E-mote 3.0 PSB inner cipher (NekoPara Vol.1) — research notes

Status: **solved and reproduced independently**. The E-mote reader decrypts a
contiguous part of every PSB container with a fixed-key xorshift128 stream.
`research/emote-psb/emote_psb.py` implements it without loading any game code,
and `research/emote-psb/test_emote_psb.py` checks it against synthetic files and
against PSBs extracted during this investigation.

Nothing here changes a game file, a game archive, or the kk4s runtime. The tools
open inputs read-only, and `decrypt` requires a separate output path.

---

## 1. What was recovered

`emoteplayer.dll` (E-mote 3.0 player) and `emotedriver.dll` (the FreeMote-facing
driver) contain the same decryption routine. It is applied once, in place, to
the PSB buffer immediately after the PSB header has been parsed and before the
name trie, strings and root object are read.

### Facts (from machine code)

| Item | `emoteplayer.dll` | `emotedriver.dll` |
|---|---|---|
| decryptor entry | VA `0x1004B920` | VA `0x100014D0` |
| vtable / slot | `0x100D7C0C`, slot 1 | `0x10071880`, slot 1 |
| state initialisation | VA `0x1004BF93` | `0x100019D5`-`0x100019F1` call site |
| PSB reader / ctor | `0x100AE730` / `0x100AE620` | `0x10001880` |
| decrypt call | `0x100AE662` | `0x100019D5` |
| key text `742877301` | `.rdata` `0x100D4394` | `.rdata` `0x10071350` |

`emoteplayer.dll` is a PE32 image with base `0x10000000`; its `.text` starts at
file offset `0x400`, so file offset `0x4B399` maps to VA `0x1004BF99` (the
immediate field of the instruction that begins at `0x1004BF93`). The 32-bit
value `0x2C46C275` there is the fourth seed. The same decimal text also occurs
at file offsets `0xB58FE` and `0xB5BDA`; the code converts it with
`strtol(s, NULL, 10)`, so the key is decimal `742877301` = `0x2C46C275`.

The initialisation block also writes the vtable pointer `0x100D7C0C`, the seeds
`123456789` (`0x75BCD15`), `362436069` (`0x159A55E5`), `521288629`
(`0x1F123BB5`), the key, and zeroes two following fields (current word and byte
counter/state). There is no per-file key material: every PSB opened by the
reader gets the same initial state.

### The algorithm

xorshift128, with the fourth word as the fixed key:

```c
if (!cur) {
    t = x ^ (x << 11);
    x = y; y = z; z = w;
    w = w ^ (w >> 19) ^ t ^ (t >> 8);
    cur = w;
}
*p ^= cur & 0xFF;
cur >>= 8;                    // logical shift
```

* The compiler split `w >> 19` into `w >> 11` followed by `>> 8`; both are
  logical shifts, so the result is identical to a single `>> 19`.
* Bytes are emitted little-endian within each 32-bit word (low byte first).
* A new word is drawn only when `cur` becomes exactly zero after four shifts.
  The code does **not** test a separate byte counter, and there is **no initial
  skip** (the first encrypted byte uses byte 0 of the first word).
* The first word for the fixed key is `0xF5AD3B8D`; the first 16 keystream bytes
  are `8d 3b ad f5 a0 6d 5f 32 cd 37 1e bc fb e4 0d ab`. This vector is
  asserted by the tests.

Both DLLs run byte-for-byte the same loop; the `emotedriver.dll` copy is a
thunk-like wrapper with the same constants.

### Encrypted region

The builder computes `len = [obj+0x30] - [obj+0x20]` and calls slot 1 of the
PSB object's vtable with `(buf + off, len)`. The two fields are

* `obj+0x20` = `buf + u32(buf+0x08)` — the **header length** field,
* `obj+0x30` = `buf + u32(buf+0x18)` — the chunk-offset table.

So the decrypted range is **[header_len, off_chunk_offsets)**. In every observed
file the header-length field equals the names offset (`0x28`), so this is also
`[off_names, off_chunk_offsets)`. Header fields, the chunk arrays and the chunk
payloads stay unencrypted.

For `002_アズキ_e-mote3.0アズキ私服a中.psb` (extracted from `emote.xp3`):

```
header_len 0x28, off_names 0x28, off_strings 0x3640d, off_strings_data 0x36867,
off_chunk_offsets 0x391ab, off_chunk_lengths 0x3929e, off_chunk_data 0x393a0,
off_entries 0x28b4
encrypted region 0x28..0x391ab = 233859 bytes
```

### Observed PSB layout

The E-mote files use the normal PSB v2 header and value encoding, but place the
root entry *before* the strings table:

```
header (0x28)
name trie (3 compact arrays: charset bases, tree, leaves)
root entry object
string offset array
string data (NUL-terminated UTF-8)
chunk offset array
chunk length array
chunk data
```

In every checked file the three trie arrays end exactly at `off_entries`
(`0x28B4` above). The validator reports this as `trie_tight` but does not
require it, since only the reader's region bounds matter for decryption.

### Verified on real data

The key was checked against **every PSB in the local NekoPara Vol.1 archives**:
79 from `emote.xp3`, 39 `dx_` from `emotewin.xp3` and 39 `dxlow_` from
`emotewin_low.xp3` (157 in total). All decrypt into valid PSBs, and re-encryption
reproduces the stored bytes exactly. See `docs/emote_psb_validation.md` for the
full archive-wide results (`validate_archives.py`).

Root objects expose keys such as `id`, `label`, `metadata`, `object`,
`screenSize`, `source`, `spec`, `stereovisionProfile` (some files also have
`easing`). For the sample above the report is 298 names, 555 strings,
80 resources.

---

## 2. The separate archive layer (do not confuse)

Files inside `emote.xp3` / `emotewin.xp3` are additionally XORed by KiriKiri's
TinyFilter with a **single byte**, derived from the file's `adlr` field
(`TinyFilterKey(adlr)`; already implemented in `compat/cxdec/tinyfilter.cpp`).
This is a different layer that wraps the whole PSB file; it has to be removed
before the inner cipher above can be applied.

Facts established on `emote.xp3`:

* The first archive entry is a plaintext warning text file; the remaining
  entries are XOR-filtered.
* The filter byte can be recovered from the magic: `d[0]^0x50 == d[1]^0x53 ==
  d[2]^0x42 == d[3]^0`.
* `adler32` of the file **after** removing that XOR (and **before** the inner
  PSB decryption) equals the `adlr` value stored by the archive index. For the
  sample above both are `0x2C74E840` (filter byte `0xD3`).

That is why `emote_psb.py --expect-adler` verifies the stored-file hash *before*
decrypting, and why its report separates `adler32_file` from `adler32_plain`.

XP3 details needed by the extractor: the `File` index entry contains `info`,
`adlr` and `segm` sub-chunks; `segm` here has a 4-byte prefix before 24-byte
`(offset, packed size, original size)` records; `flags & 1` means zlib. Real
names come from a custom `eliF` chunk (`u32 adlr`, `u16 length`, UTF-16LE), not
from `info.name`.

---

## 3. Hypotheses / not verified

* **Key is per-build, not per-file.** All tested files share it, and the key is
  baked into both DLLs as a constant. Whether other E-mote 3.0 builds or other
  games use `742877301` is untested.
* **`emotedriver.dll` block at `0x100015B6`.** It initialises the same xorshift
  seeds but uses `time()` (import `[0x1006F020]`, call `0x1005B368`, divided by
  10,000,000) as the fourth word. This is a runtime RNG, not the PSB cipher;
  it was not investigated further.
* **`dx_` archive layer verified.** `adler32(unfiltered) == adlr` and the
  `TinyFilterKey(adlr)` formula hold for all 39 `dx_` and all 39 `dxlow_` files
  (157/157 PSB overall), see `docs/emote_psb_validation.md`.
* **PSB versions.** Only version 2 was observed. The reader's destructor
  branches on versions 0/2/3/4, and the tool accepts 2/3/4 with the same
  layout; version 0/1 and any nonzero `encrypt` flag are rejected rather than
  guessed.
* **Why `header_len == off_names`.** The reader uses the header-length field as
  the region start and the names offset when walking the trie; all observed
  files have both at `0x28`. The tool refuses files where they differ.
* **The `encrypt` header field** is zero in every observed file. The inner
  cipher is unconditional in the reader; a nonzero value may select a different
  path that is not covered here.

---

## 4. PoC usage

```sh
# summary only, writes nothing
python3 research/emote-psb/emote_psb.py inspect FILE.psb

# decrypt into a new file (input is never modified)
python3 research/emote-psb/emote_psb.py decrypt FILE.psb -o OUT.psb

# archive-filtered file, optional archive-index hash check
python3 research/emote-psb/emote_psb.py decrypt FILE.psb -o OUT.psb \
    --xp3-xor auto --expect-adler 2c74e840
```

Options: `--key` (32-bit, default `742877301`), `--xp3-xor auto|none|0xNN`,
`--expect-adler HEX`, `--force`, `--json` (inspect).

Safety behaviour:

* refuses an output path that resolves to the input, even with `--force`;
* refuses to replace an existing output without `--force`;
* validates magic, version/flag, section order, trie, strings, chunk table,
  root object and all indices *before* writing;
* writes through a temporary file and `os.replace`.

`decrypt` is **not idempotent**: the output is a plain PSB, and running the tool
on it again XORs the region a second time and fails validation. That is why
feeding an already-decrypted file is rejected rather than silently accepted.

Other tools in the same directory (session aids, no game data):

* `pe_disasm.py` — PE32 VA/file mapping, capstone disassembly, `--find-bytes`,
  `--xrefs`.
* `extract_xp3.py` — XP3 v2 reader, `eliF` names, 4-byte `segm` prefix, refuses
  to extract into the archive's own directory.

## 5. Tests

```sh
python3 research/emote-psb/test_emote_psb.py -v
```

29 unit tests plus an archive-wide validator:

* keystream vector (`8d3badf5a06d5f32...`) and one word per four bytes;
* cipher involution, region-only changes, bounds checks;
* synthetic PSB v2/v3 round-trips (nested objects/arrays, UTF-8 names and
  strings, one resource, floats, booleans, null);
* wrong key, bad magic, truncation, nonzero `encrypt`, mismatched header length,
  out-of-order offsets, entry offset past EOF;
* XP3 filter `auto`/`none`/garbage, adler pass/fail;
* CLI: explicit output, same-file refusal (with `--force`), existing-output
  refusal, atomic replace, inspect writes nothing;
* optional real-sample test: four `emote.xp3` PSBs plus two `dx_` PSBs from
  `work/` (gitignored) must decrypt, report non-empty names/strings/root, and
  re-encrypt to the exact stored bytes.

`validate_archives.py` reads all 157 PSB from the three local archives and
checks Adler-32, TinyFilter, structure and round-trip (see
`docs/emote_psb_validation.md`).

`research/emote-psb/work/` (extracted game data) is ignored by `.gitignore` and
is not part of any commit.

---

## 6. Where the investigation stopped

Solved for NekoPara Vol.1 x86 E-mote 3.0 and validated against all 157 local
PSB. Not done, by design: no game file was patched or repacked, no PSB support
was added to kk4s, and E-mote rendering was out of scope. The remaining
uncertainty is the portability of the fixed key to other E-mote builds
(`docs/emote_psb_validation.md` has a concrete verification plan).
