# SD card layout

```
sdmc:/switch/kk4s/
├── kk4s.nro
├── last.txt                 last played game (launcher)
├── settings.ini             launcher settings + play history (language, layout, sort, ...)
├── games/
│   └── <GameName>/          original game folder, untouched (data.xp3, patch*.xp3, video/, ...)
├── covers/<GameName>.jpg    optional cover art for the launcher (png / jpg / webp)
├── fonts/                   extra fonts; fonts/.cache/ = Nintendo system font copy
├── logs/                    kk4s-YYYYMMDD-HHMMSS.log, one per run
├── saves/<GameName>/        save data (setting "separate saves", on by default): every file access
│                            to <game>/savedata/ goes here; existing saves are copied on the first start
├── saves/.backup/<GameName>/<date>/   backups made in the game options ("deleted-<date>" = deleted saves)
├── .from_game               marker: the launcher was restarted after a game (no autostart)
├── overlay/<game_id>/       (M2) replacement files, override xp3 contents
└── profiles/<game_id>.json  (M3) per-game profiles supplied by the user
```

All folders are created on first start (except `covers/`).

## Launcher covers and titles

The launcher shows a card per game. Cover, first found wins:

1. `covers/<GameName>.*` (png, jpg, webp). **Y** in the launcher (or tap "Обкладинка") searches
   [VNDB](https://vndb.org) for the selected game, shows the matches, and saves the chosen cover here
   together with `covers/<GameName>.txt` (the official title). Covers VNDB marks as sexual are skipped.
2. `games/<GameName>/cover.*`, `folder.*` or `poster.*` — files you add next to the game yourself.
3. Otherwise a colored card with the game's icon (a `*.ico` in the folder, or the icon inside the
   game `.exe`) and the title.

Title: first line of `games/<GameName>/title.txt`, then `covers/<GameName>.txt` (UTF-8); otherwise
the folder name. Portrait images (about 5:7) fit the card best.

Launcher controls: D-pad / left stick / swipe = choose, A / tap the selected card = play,
Y = cover search, X / gear = settings, + = quit. In the search: A = take this cover,
X = edit the search text (Switch keyboard; on PC type in place), B = back.
The search cleans the title first ("Tsukihime Plus+Disc Russian" -> "Tsukihime Plus Disc") and
retries with fewer words when VNDB finds nothing.

Settings (X): language (auto / Українська / English / Русский / 日本語), layout (carousel / grid),
sort (title / recently played), cover background, hide explicit covers, separate saves, return to
the launcher after a game, autostart the last game (3 s countdown, any button cancels), controls
(what A, B, X, Y, L, R, ZL, ZR, +, − do in games), download all missing covers.

Game options (−): cover from the internet, remove the downloaded cover, back up saves, restore the
latest backup, delete saves (moved to `saves/.backup/<GameName>/deleted-<date>/`). Restore and delete
ask for a second press; restore first backs up the current saves.

In-game menu: + or − (or F1 on PC) opens the kk4s menu over the game: resume, quick save / quick
load (KAG bookmark 900, a slot the game's own save screens do not use), volume, controls, exit to
the launcher, quit kk4s (both ask for a second press). Which buttons open it is set in Controls
("kk4s menu"). `KK4S_OVERLAY_SHOT=<file.bmp>` saves the menu once shown (PC).

Debugging aids (PC): `KK4S_LAUNCHER_SHOT=<file.bmp>` saves one launcher frame and quits;
`KK4S_LAUNCHER_SEARCH=1` opens the cover search first (`=pick` also takes the first result);
`KK4S_LAUNCHER_SHEET=settings|controls|game` opens that sheet first;
a `%d` in `KK4S_LAUNCHER_SHOT` saves frames 30..45 while scrolling.
