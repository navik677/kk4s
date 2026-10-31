# Tests

Synthetic scripts only — no game content.

- `tjs/hello/` — smallest possible "game": checks that the engine boots, opens a window and logs.
  CI runs it for 15 s under xvfb (Linux, ASan + UBSan) as a smoke test.
- `tjs/input/` — shows every key / mouse event the game receives, plus `System.getKeyState(VK_CONTROL)`.
  Copy to `games/kk4s-input-test/` to check the gamepad / touch mapping on a device.

Later: a TJS2/KAG conformance suite and synthetic xp3 archives for the compat layer.
