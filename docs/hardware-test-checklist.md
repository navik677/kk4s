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
- [x] In-game menu (+ / −)
- [x] Launcher carousel without flicker (pre-rendered cards)

## Open

- [ ] Applet (album) mode memory limits
- [ ] Video playback (FFmpeg software)
- [ ] Docked ↔ handheld (1080p ↔ 720p) while running
- [ ] Sleep / resume, HOME button during a game
- [ ] Long sessions: memory growth, texture cache
- [ ] Audio latency / voice sync
