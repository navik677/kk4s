# Launcher and in-game menu

Code: `platform/common/kk4s_launcher.cpp` (screens), `kk4s_ui` (drawing), `kk4s_i18n` (texts),
`kk4s_settings` (`settings.ini`), `kk4s_net` (VNDB over HTTPS), `kk4s_files` (save backups),
`kk4s_overlay` (in-game menu). The launcher draws into the engine's SDL window before the game starts.

## Home screen

A carousel (or grid) of cards over a blurred copy of the selected cover. Under the title: last
launch, number of launches, folder name. Top bar: clock, battery, settings button.

| Input | Action |
|---|---|
| D-pad / left stick / swipe / mouse wheel | choose |
| A / tap the selected card / Enter | play |
| Y / F3 | cover search |
| − / O / F4 | game options |
| X / gear / F2 | settings |
| L / R / Page Up / Page Down | jump 5 games (carousel) or 2 rows (grid) |
| + / Esc | quit kk4s |

On Switch the buttons are read from libnx by their printed labels (`kk4s::ReadPad`): SDL's game
controller mapping is positional and swapped A/B. PC pads use SDL game controller events.

## Covers and titles

Cover, first found wins:

1. `covers/<GameName>.*` (png, jpg, webp) — where the cover search saves.
2. `games/<GameName>/cover.*`, `folder.*` or `poster.*` — files you add yourself.
3. Otherwise a generated card with the game's icon (`*.ico` in the folder, or the icon inside the
   game `.exe`) and the title.

Title: first line of `games/<GameName>/title.txt`, then `covers/<GameName>.txt` (written by the
cover search), otherwise the folder name. Portrait images (about 5:7) fit best.

Cards are rendered once into textures (`BakeCard`) and only copied each frame; drawing them with
textured geometry every frame flickered on the Switch.

## Cover search (VNDB)

Y opens a search for the selected game on [VNDB](https://vndb.org) (`api.vndb.org/kana`, no key):

- The title is cleaned first: no `[tags]`, `(notes)`, versions, language / "patch" words; `+ - _ .`
  become spaces ("Tsukihime Plus+Disc Russian" → "Tsukihime Plus Disc"). With no results the last
  word is dropped and the search repeated.
- Results are re-ranked: titles containing the query as whole words first (VNDB's search is fuzzy).
- Covers VNDB users voted as sexual are hidden (setting).
- A takes the cover: saved as `covers/<GameName>.<ext>` plus `covers/<GameName>.txt` (official title).
- X edits the search text: the Switch software keyboard (swkbd), or typing in place on PC.

"Download all covers" (settings) takes the first result for every game without a cover, but only
when its title matches the query as whole words.

Network work runs on detached SDL threads with a 1 MiB stack; the UI polls the job.

## Settings (X)

| Setting | Values |
|---|---|
| Language | auto (system language; the Switch has no Ukrainian UI, so pick it by hand), Українська, English, Русский, 日本語 |
| Layout | carousel / grid |
| Sort | by title / recently played |
| Cover background | on / off |
| Hide explicit covers | on / off (VNDB filter) |
| Separate saves | on (default) / off — see below |
| Return to the launcher after a game | on (default) / off |
| Autostart the last game | 3 s countdown, any input cancels; not after coming back from a game |
| Controls | action per pad button (A, B, X, Y, L, R, ZL, ZR, +, −) in games; defaults + = Esc, − = Tab. L3 + R3 (menu) is fixed |
| CPU boost (Switch) | on (default): 1785 MHz instead of 1020 MHz while a game runs (`appletSetCpuBoostMode(FastLoad)`, the GPU drops to its minimum, which is plenty for one textured quad) |
| Download all covers | see above |

Stored in `<data>/settings.ini` with the play history (`played.<folder>=<time> <count>`) and the
pad mapping (`pad.<button>=<action>`).

## Game options (−)

Cover from the internet; remove the downloaded cover; save info (files, size, last save, folder);
back up saves (`saves/.backup/<GameName>/<date>/`); restore the latest backup (the current saves are
backed up first); delete saves (moved to `saves/.backup/<GameName>/deleted-<date>/`). Restore,
delete and remove cover ask for a second press.

## Saves

With "separate saves" (default) every local file access under `<game>/savedata/` and under the
engine's original data path is mapped to `<data>/saves/<GameName>/`
(`compat::RedirectSavePath`, called from `StorageImpl.cpp`). KAG builds `<exe>/savedata/...`
itself, so changing only the engine data path is not enough. On the first start of a game its
existing save files are copied over; the originals stay where they are.

## In-game menu

Both sticks pressed together (L3 + R3; F1 on PC) open and close a menu over the game. + and − are
left to the game (Esc / Tab by default); any button can still be set to "kk4s menu" in Controls. The
game gets no input while the menu is open (held keys are released first).

- Resume
- Quick save / quick load: KAG bookmark 900 (`kag.saveBookMark(900)` / `loadBookMark`), a slot the
  game's own save screens do not show. Games without the standard KAG functions show "not supported".
  A bookmark does not contain KAG's click / key hooks, so quick save also writes the hook counts
  (`data900.kdt.kk4s`) and quick load drops hooks added after that (e.g. "click skips the logo"
  from The Makings of a Lady, which broke the first click after loading during the logo).
- Volume: 0–100 % in steps of 10 (`WaveSoundBuffer.globalVolume`), saved and applied at the next start.
- Controls: same list as in the launcher.
- Exit to the launcher / quit kk4s (second press): `kag.saveSystemVariables()` first, then exit.

The menu is drawn over the last game frame each frame (`kk4sOverlayFrame` in `sdl2/stubs.cpp`);
while it is visible the engine's own present is skipped.

## Returning to the launcher

`kk4s::ExitAfterGame`: on Switch it asks hbloader to start `kk4s.nro` again (`envSetNextLoad`),
except when the system is closing the app (HOME → close). This needs libnx' default
`__nx_applet_exit_mode` (0): with 1 (inherited from krkrs) an application in title takeover closed
itself on exit and hbloader never got control back, so "exit to the launcher" quit kk4s; on PC it re-executes itself when the game
was picked in the launcher. `<data>/.from_game` tells the next start to skip autostart.

## Debugging aids (PC)

| Variable | Effect |
|---|---|
| `KK4S_LAUNCHER_SHOT=<file.bmp>` | save one launcher frame and quit (`%d` in the name: frames 30–45 while scrolling) |
| `KK4S_LAUNCHER_SEARCH=1` / `pick` | open the cover search first (`pick`: also take the first result) |
| `KK4S_LAUNCHER_SHEET=settings` / `controls` / `game` | open that sheet first |
| `KK4S_OVERLAY_SHOT=<file.bmp>` | save the in-game menu once it is fully shown |
| `SDL_RENDER_DRIVER=opengles2` | the renderer the Switch uses |

Screen capture of the SDL window does not work on KDE Wayland; use these instead.
