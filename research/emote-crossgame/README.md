# E-mote cross-game research tools

Read-only helpers for checking whether the PSB cipher recovered from NekoPara
Vol.1 also applies to other E-mote builds.  They do not patch, repack or execute
game code, and they never write next to an input file.

| Tool | Purpose |
|---|---|
| `audit_dll.py` | SHA-256, VS_VERSIONINFO, key string/immediate, seed constants, code references to the key string |
| `scan_environment.py` | Inventory binaries and loose `.psb` files under one or more roots; JSON output |
| `inspect_sample.py` | Validate a PSB file or one XP3 entry: filter, Adler-32, structure, round-trip, region bounds |
| `test_crossgame_tools.py` | Unit tests using synthetic PSBs only (no game files) |

## Examples

```sh
# Hashes and cipher-constant evidence in a plugin
python3 research/emote-crossgame/audit_dll.py path/to/emoteplayer.dll \
    --json-out research/emote-crossgame/work/audit_dll.json

# What is present locally (default root: ./game)
python3 research/emote-crossgame/scan_environment.py

# A loose sample, or one entry extracted in memory from an XP3 archive
python3 research/emote-crossgame/inspect_sample.py sample.psb --key 742877301
python3 research/emote-crossgame/inspect_sample.py \
    --xp3 game/SomeGame/emote.xp3 --entry 1 --json

# Tests
python3 research/emote-crossgame/test_crossgame_tools.py -v
```

`inspect_sample.py` exits non-zero unless the PSB parses, re-encrypts
byte-identically, keeps bytes outside the cipher region unchanged, and (when an
`adlr` is known) matches the archive Adler-32.  `filter_method` reports whether
the archive layer was `none`, `magic-xor`, or `tinyfilter`, so the two layers
are never conflated.

All scan/audit output belongs in `work/`, which is gitignored.  Game data, DLLs,
PSB and XP3 files must never be committed.
