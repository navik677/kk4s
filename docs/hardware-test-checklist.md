# Hardware test checklist

Only things that really need a Switch. Keep sessions short.

## Done

- [x] `kk4s.nro` in hbmenu with icon and name
- [x] Launcher lists games, A starts one, + quits; empty `games/` shows a hint
- [x] Log file with git hash, memory line and `mode: full` in title takeover
- [x] Real games run (FSN, Sharin no Kuni, Tsukihime, The Makings of a Lady)
- [x] Gamepad (libnx, by label) and touch → mouse
- [x] Saving and loading in game; saves in `saves/<game>/`
- [x] Cover search over HTTPS (VNDB, switch-curl)
- [x] Game quit returns to the launcher
- [x] In-game menu (was + / −; now L3 + R3)
- [x] Launcher carousel without flicker (pre-rendered cards)

## Open

- [ ] In-game menu with L3 + R3; + / − reach the game as Esc / Tab
- [ ] "Exit to the launcher" from the in-game menu comes back to the launcher (exit mode fix)
- [ ] Known from the 2026-10-31 log (The Makings of a Lady): ~2 frames of 200–250 ms at the start of
      long text pages, 1–4 s after some page ends (the game's `[tp]` macro calls `AutoSave()`); the new
      `slow frame` fields tell which part is slow
- [ ] Performance: worker threads on cores 1–2 (`threads:` lines in the log), CPU boost
      (`perf: CPU boost on`); compare stutter with the boost off; send the log (`slow frame`,
      `xp3: decrypted ... ms` lines)
- [ ] Applet (album) mode memory limits
- [ ] Video playback (FFmpeg software)
- [ ] Docked ↔ handheld (1080p ↔ 720p) while running
- [ ] Sleep / resume, HOME button during a game
- [ ] Long sessions: memory growth, texture cache
- [ ] Audio latency / voice sync
