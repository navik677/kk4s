# E-mote PSB decoder — validation report

Scope: every E-mote PSB available in the local NekoPara Vol.1 installation,
three XP3 archives, plus the synthetic and CLI test suite. Nothing in
`game/NekoPara Vol.1/` was modified; the archives were opened read-only.

Reproduce with:

```sh
python3 research/emote-psb/test_emote_psb.py -v
python3 research/emote-psb/validate_archives.py
```

The machine-readable result is `research/emote-psb/work/validation.json`
(gitignored), full console log in `work/validation.log`.

---

## 1. PSB files found

| Archive | Entries | PSB | Non-PSB |
|---|---:|---:|---|
| `emote.xp3` | 88 | **79** | warning `.txt` + 8 `.mstand` |
| `emotewin.xp3` | 40 | **39** (`dx_…`) | warning `.txt` |
| `emotewin_low.xp3` | 40 | **39** (`dxlow_…`) | warning `.txt` |
| **total** | **168** | **157** | 11 |

`emotewin_low.xp3` was included as an additional available archive even though
the sprint only required `emote.xp3` and `emotewin.xp3`.

Other archives in the installation (`patch.xp3` 222 entries, `data.xp3` 1201,
`evimage.xp3` 94) contain **no** `.psb` files.

## 2. Successfully decrypted

**157 / 157** PSB. Every one passed:

* PSB magic, version 2, `encrypt` flag 0;
* `header_len == off_names == 0x28`;
* strictly increasing section offsets, chunk table inside the file;
* name trie decode with all leaf indices valid;
* string table decode with all string indices resolving;
* chunk offset/length arrays of equal length, all payloads inside the file;
* root value is a non-empty object (226–476 names, 384–573 strings,
  3–82 resources per file).

Encrypted region sizes: 190,373–265,768 bytes; total PSB bytes 2,053,260,904.

## 3. Round-trip

**157 / 157** byte-identical. After decryption the region was re-encrypted with
the same cipher and compared with the stored (unwrapped) file; no differences.
Bytes outside the region were also asserted unchanged (157/157), and a
reference-prefix cross-check against `emote_psb.crypt_region` (the original
pure-Python implementation) matched for every file (157/157).

Caveat: XOR round-trip alone is self-inverse and would also "pass" with a wrong
keystream. Keystream correctness is established separately by all 157 files
parsing into coherent PSB structures, and by the 64 KiB prefix comparison with
the reference implementation.

## 4. Adler-32 results

| Subset | Entries | Adler matches | Mismatches |
|---|---:|---:|---:|
| `emote.xp3` PSB (normal) | 79 | **79** | 0 |
| `emotewin.xp3` PSB (`dx_`) | 39 | **39** | 0 |
| `emotewin_low.xp3` PSB (`dxlow_`) | 39 | **39** | 0 |
| **all PSB** | **157** | **157** | **0** |

This closes the open question from the previous report: the `adler32`
equality holds for **all** available `dx_` files, not just the normal ones.

Archive-layer facts:

* filter bytes recovered: 126 distinct single-byte keys across 157 files;
* all 157 filtered files match `TinyFilterKey(adlr)` from
  `compat/cxdec/tinyfilter.cpp` (0 mismatches);
* `adler32(unfiltered) == adlr` for every PSB entry.

Non-PSB entries: the 8 `.mstand` files are stored unfiltered and their
`adler32` equals `adlr`. The 157-byte plaintext warning file (present once per
archive) has `adlr = 0x689792E4` but `adler32 = 0xB1354C15`; it is not a PSB and
the archive appears to store a placeholder hash for it. This does not affect the
decoder and is reported, not "fixed".

## 5. Test suite

Command: `python3 research/emote-psb/test_emote_psb.py -v`

Result: **Ran 35 tests — OK** (7.7 s).

The sprint started at 29 tests; six regression tests were added (section 6).
Coverage includes the keystream vector, synthetic v2/v3 round-trips, every
header/offset rejection path, XP3 filter modes, Adler pass/fail, CLI behaviour,
atomic output, and optional local samples.

## 6. Bugs found and fixes

No defect was found in the decoder (`emote_psb.py` was not changed in this
sprint). Two defects were in the newly written archive validator:

1. **Fast keystream was not byte-exact.** It emitted four bytes per xorshift
   word, but the reader draws a new word as soon as the shifted value becomes
   zero — roughly 1/256 of words yield only three bytes. The stream diverged
   within the first few KiB. Fixed to a byte-exact generator; the reference
   implementation was correct all along.
2. **Reference-prefix comparison used a wrong slice.** It compared the whole
   decrypted prefix with the region slice. Fixed to `ref[start:]`.

Additional regression tests added for previously untested safety properties:

* wrong key creates no output file and no temp files;
* re-running `decrypt` on an already-decrypted file fails cleanly, leaves the
  file and directory untouched;
* output hardlinked to the input is refused even with `--force`;
* output symlinked to the input is refused even with `--force`;
* atomic write leaves no `.emote_psb.*.tmp` files;
* corrupt ciphertext (first trie byte flipped) is rejected and writes nothing.

## 7. Really verified vs hypothesis

**Verified on this machine:**

* cipher algorithm, key `742877301`, region `[header_len, off_chunk_offsets)`,
  no initial skip, early word refill, little-endian byte order — across 157 real
  files from three archives;
* structural validity of every decrypted file, including trie tightness
  (`trie_end == off_entries` in 157/157) and `header_len == off_names`;
* re-encryption byte equality and region-only modification;
* TinyFilter key formula and `adler32 == adlr` for every PSB, including all
  `dx_`/`dxlow_` entries;
* safety behaviour listed above;
* input archives unchanged (size and `mtime_ns` before/after; only read-only
  opens).

**Still hypothesis / not covered:**

* **Other E-mote 3.0 builds.** No PSB or DLL from another game/version is
  available here. The fixed key is baked into this NekoPara Vol.1 build; it is
  plausible but unproven that other builds share it.
* **PSB versions 3/4.** Accepted by the tool with the same layout, but only
  version 2 files were observed.
* **Nonzero `encrypt` header flag.** Rejected; behaviour of such files unknown.
* **Reason for `header_len == off_names`.** Always true here; other layouts are
  rejected rather than guessed.
* **`emotedriver.dll` time()-seeded xorshift block** is a runtime RNG, not this
  cipher; not investigated further.

**Plan to establish compatibility (not executed):**

1. Obtain one PSB and the corresponding `emoteplayer.dll` from another E-mote
   3.0 title; record game/build, file sizes and SHA-256 of both.
2. Search that DLL for ASCII `742877301`, the constants `0x2C46C275`,
   `0x75BCD15`, `0x159A55E5`, `0x1F123BB5`, and the decryptor vtable slot;
   diff against the NekoPara build.
3. Run `emote_psb.py inspect` on the PSB. If it validates, run the same
   archive-wide validation and record `adler32`/filter-key results. If the key
   differs, recover the fourth word from the init block and retry with `--key`.
4. Only after samples from at least one independent build pass without a custom
   key should the decoder be described as portable across E-mote 3.0.

## 8. Git state

* Branch: `research/emote-psb` (only this branch was touched).
* Base commit before the sprint: `cc5dfc6`.
* Validation commit (decoder untouched, validator + tests + this report):
  `8700b9b`.
* Push state: **pushed** to `origin/research/emote-psb`; the branch was created
  on the remote by this push. A docs-only follow-up commit records this line.
* `main` and `cloud-session` were not modified.

Artifacts added by this sprint:

* `research/emote-psb/validate_archives.py` — full-archive validator;
* six regression tests in `research/emote-psb/test_emote_psb.py`;
* this report.

`research/emote-psb/work/` (validation JSON/log, samples, test outputs) remains
gitignored; no game resource is part of any commit.
