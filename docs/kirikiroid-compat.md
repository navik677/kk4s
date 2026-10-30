# Kirikiroid2 compatibility

Many game folders on the internet are already prepared for **Kirikiroid2** (Android). kk4s should run those folders as-is, because they solve the hard part (encryption, plugin quirks) with small TJS files placed next to the game — no changes to the original archives.

Files seen in a real Kirikiroid2-ready folder (FSN Realta Nua + fan patch, tested locally, not in the repo):

| File | What Kirikiroid2 does with it | kk4s plan |
|---|---|---|
| `xp3filter.tjs` | Executed before the game. Defines a decrypt function and calls `Storages.setXP3ArchiveExtractionFilter(func)`. `func(hash, offset, buffer, length)` modifies the read buffer in place (`buffer.xor(ofs, len, value)`, indexing). | **M5**: implement `Storages.setXP3ArchiveExtractionFilter` + the buffer object, run `xp3filter.tjs` from the game folder before `startup.tjs`. Keys stay in the user's file. |
| `Override2.tjs` | Executed after the game's `Override.tjs` / system init; patches KAG tag handlers etc. | **M6**: run it at the same point (hook layer). |
| `AfterInit2.tjs` | Executed after `kag` is created. | **M6**: same. |
| `cxdec.tpm` | Windows TPM plugin with the original decryption (not usable). | Ignore, log it. |
| `*.dll` (wuvorbis, krmovie, extrans, dirlist, ...) | Mapped to built-in plugins. | **M4** plugin table. |

The xp3 archives in such folders start with the normal `XP3\r\n \n\x1a\x8bg\x01` header; only file contents are encrypted, the index is readable.

Rule: never commit these scripts from real games. Tests use synthetic filters (`tests/`).
