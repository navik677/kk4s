# Tests

Synthetic scripts only — no game content.

- `tjs/hello/` — smallest possible "game": copy the folder to `sdmc:/switch/kk4s/games/hello/` to check that the engine boots, opens a window and logs.

Later (M1/M6): run these on the `linux-sdl2` target under ASan/UBSan in CI, plus a TJS2/KAG conformance suite.
