# Tests

Synthetic scripts only — no game content.

- `tjs/hello/` — smallest possible "game": checks that the engine boots, opens a window and logs.
  CI runs it for 15 s under xvfb (Linux, ASan + UBSan) as a smoke test.
- `tjs/input/` — shows every key / mouse event the game receives, plus `System.getKeyState(VK_CONTROL)`.
  Copy to `games/kk4s-input-test/` to check the gamepad / touch mapping on a device.

Generated fixtures (they create their own temporary game folders and archives):

- `xp3/elif_smoke.py` — XP3v2 `eliF` filename mapping.
- `xp3/empty_entry_smoke.py` — a zero-byte entry with an empty `segm` chunk opens, reads, seeks and rejects a seek past the end.
- `xp3/malformed_index_smoke.py` — a corrupt index is rejected instead of looping or reading out of bounds.
- `xp3/index_cycle_smoke.py` — a self-referential index chain is rejected instead of looping forever.
- `textstream/smoke.py` — TextStream encoding detection (ASCII, Shift_JIS, UTF-8/BOM, GBK, malformed).
- `alphamovie/smoke.py` — AlphaMovie AJPM zlib-alpha decode.
- `pbd/run_tests.py` — `TJS/ns0` / `TJS/4s0` data-pack decoder against a Python encoder (all value
  types, all six crypt modes, dictionary LZ4, truncation/corruption). Pass a directory of real
  `.pbd` files as the second argument to check those too.
- `pbd/loaddatapack_smoke.py` — engine integration: `Scripts.loadDataPack` returns real TJS
  Array/Dictionary trees and rejects an invalid pack.

Later: a TJS2/KAG conformance suite.
