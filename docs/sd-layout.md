# SD card layout

```
sdmc:/switch/kk4s/
├── kk4s.nro
├── games/<GameName>/              original game folder, never changed (data.xp3, patch*.xp3, video/, ...)
├── saves/<GameName>/              save data ("separate saves", default): accesses to <game>/savedata/
│                                  land here; existing saves are copied on the first start
├── saves/.backup/<GameName>/      backups from the game options (<date>/, <date>-before-restore/, deleted-<date>/)
├── covers/<GameName>.{jpg,png,webp}  cover art (downloaded from VNDB or your own)
├── covers/<GameName>.txt          official title saved by the cover search
├── fonts/                         extra fonts; fonts/.cache/ = copies of the Nintendo system fonts
├── logs/kk4s-YYYYMMDD-HHMMSS.log  one log per run
├── settings.ini                   settings, play history, pad mapping
├── last.txt                       last played game
├── .from_game                     marker: started again after a game (skip autostart)
├── overlay/                       reserved (M2: replacement files over xp3 contents)
└── profiles/                      reserved (M3: per-game profiles)
```

Folders are created on the first start (`covers/` when the first cover is saved).

A folder in `games/` is a game when it contains `startup.tjs` or any `*.xp3`. The project dir is
chosen like `krkr.exe` does: `startup.tjs` → `data.xp3` → `data/` → the folder itself
(case-insensitive). The game folder acts as the folder of the exe (`System.exePath`).

Files you may add next to a game without touching its own files: `title.txt` (display name, UTF-8),
`cover.*` / `folder.*` / `poster.*` (cover art). See [launcher.md](launcher.md).

On the PC viewer the same layout lives next to the executable, or in `$KK4S_ROOT`.
