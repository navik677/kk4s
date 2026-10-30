# SD card layout

```
sdmc:/switch/kk4s/
├── kk4s.nro
├── last.txt                 last played game (launcher)
├── games/
│   └── <GameName>/          original game folder, untouched (data.xp3, patch*.xp3, video/, ...)
├── covers/<GameName>.jpg    optional cover art for the launcher (png / jpg / webp)
├── fonts/                   extra fonts; fonts/.cache/ = Nintendo system font copy
├── logs/                    kk4s-YYYYMMDD-HHMMSS.log, one per run
├── saves/<game_id>/         (M2) redirected save data
├── overlay/<game_id>/       (M2) replacement files, override xp3 contents
└── profiles/<game_id>.json  (M3) per-game profiles supplied by the user
```

All folders are created on first start (except `covers/`).

## Launcher covers and titles

The launcher shows a card per game. Cover, first found wins:

1. `games/<GameName>/cover.*`, `folder.*` or `poster.*` (png, jpg, jpeg, webp). These are extra
   files you add next to the game; the game itself is not changed.
2. `covers/<GameName>.*` (same formats) — keeps the game folder exactly as it was.
3. Otherwise a colored card with the game's icon (a `*.ico` in the folder, or the icon inside the
   game `.exe`) and the title.

Title: first line of `games/<GameName>/title.txt` (UTF-8); without it the folder name is shown. Portrait images (about 5:7) fit the card best.

Debugging aid: `KK4S_LAUNCHER_SHOT=<file.bmp>` saves one launcher frame and quits (PC).
