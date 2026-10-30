# Hardware test checklist

Only things that really need a Switch. Keep sessions short.

## M0
- [ ] `kk4s.nro` appears in hbmenu with icon and name "kk4s"
- [ ] Launcher shows "No games found" with empty `games/`, `+` exits back to hbmenu
- [ ] Launcher lists folders in `games/`, A starts a game
- [ ] Log file is created in `logs/` and contains git hash + memory line
- [ ] Log shows `mode: title takeover` when started with R held
- [ ] Simple unencrypted test game (synthetic `startup.tjs`, see `tests/`) shows a window

## Later
- [ ] Applet mode (album) memory limits
- [ ] Performance on a real VN (scene transitions, text speed)
- [ ] Video playback (FFmpeg software; NVDEC if added)
- [ ] Touch → mouse
- [ ] Audio latency
- [ ] Sleep / resume, HOME button
- [ ] Docked ↔ handheld switch (1080p ↔ 720p)
