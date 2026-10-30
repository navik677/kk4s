# SD card layout

```
sdmc:/switch/kk4s/
├── kk4s.nro
├── last.txt                 last played game (launcher)
├── games/
│   └── <GameName>/          original game folder, untouched (data.xp3, patch*.xp3, video/, ...)
├── logs/                    kk4s-YYYYMMDD-HHMMSS.log, one per run
├── saves/<game_id>/         (M2) redirected save data
├── overlay/<game_id>/       (M2) replacement files, override xp3 contents
└── profiles/<game_id>.json  (M3) per-game profiles supplied by the user
```

All folders are created on first start.
