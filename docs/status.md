# Status

## M0 — first build

Works (by design, needs hardware confirmation):
- CI builds `kk4s.nro` + `kk4s.elf` in `devkitpro/devkita64`, nightly release on every push to `main`.
- Console launcher: scans `sdmc:/switch/kk4s/games/`, remembers last played (`last.txt`), A = play, + = exit.
- Game folder becomes the "exe folder" (`System.exePath`), so `patch.xp3` / `data.xp3` behave like on Windows.
- Project dir: `startup.tjs` → `data.xp3>` → `data/` → folder (case-insensitive).
- Log file per run in `sdmc:/switch/kk4s/logs/`, fsync after every line; build id (git hash), memory and applet/takeover mode logged at start. Engine console log goes there too.
- nxlink stdio when launched with `nxlink -s`.

Not done / not checked:
- Not run on real hardware yet → see [hardware-test-checklist.md](hardware-test-checklist.md).
- Only what krkrs supported: unencrypted XP3, internal plugins listed in `engine/README.upstream.md`. Most commercial games will stop at a missing plugin or encrypted xp3.
- Non-ASCII game folder names are not tested (narrow → wide conversion of paths).
- No large-stack thread for TJS yet; no memory budget.
- Saves are written wherever the game asks (probably inside the game folder) — redirect comes in M2.
