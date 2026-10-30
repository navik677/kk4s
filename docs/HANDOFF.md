# Handoff — read this first in a new session

Last update: 2026-10-30 (second session, Fedora Linux). Goal of the project: **any Kirikiri (KAG/TJS2) game folder runs unmodified** on Switch (and on PC via the viewer). No patching of game files; fixes live in the engine, `compat/`, profiles or the overlay folder. See `docs/original-prompt.md` for the full requirements and `docs/roadmap.md` for milestones.

## Where we are

- **Switch**: `kk4s.nro` builds in CI (GitHub Actions, `devkitpro/devkita64`), published to the `nightly` release. **Never run on real hardware yet.**
- **PC viewer** (same engine, `platform/desktop/`): builds on Linux (CI, ASan+UBSan, smoke test) and Windows (CI + locally with MSYS2).
- **Real game test: Fate/stay night** (Realta Nua + Russian fan patch, a folder prepared for Kirikiroid2, encrypted xp3) **plays on the Windows PC viewer**: intro, prologue, Russian text, dialogs, no script errors in the log. The game is NOT in the repo (`game/` is git-ignored) — copy your own game folder to `game/<name>/` on the new PC.

Details: `docs/status.md`, `docs/kirikiroid-compat.md`, `docs/plugins.md`, `docs/pc-viewer.md`.

## Repo map

| Path | What |
|---|---|
| `engine/` | krkrs (uyjulian) sources. Our edits are marked with `kk4s:` comments (`git grep kk4s: engine/`). Commit `98ac529` is the pristine upstream import. |
| `platform/common/` | logger, game scan, project dir, font file scan, small POSIX shims (`kk4s_fs.h`), graphical launcher (`kk4s_launcher.cpp`: covers, icons from .ico/.exe, gamepad/touch) |
| `platform/switch/`, `platform/desktop/` | launcher, paths, memory, window config, system fonts per platform |
| `compat/kk4s_compat.cpp` | Kirikiroid2 conventions (`xp3filter.tjs`, `Storages.setXP3ArchiveExtractionFilter`, `Override2.tjs`, `AfterInit2.tjs`), engine log → kk4s log, font registration, missing-plugin log |
| `compat/kk4s_plugins.cpp` | table `dll name → native replacement` (util.dll, p.dll, …) |
| `compat/kk4s_layerfx.cpp` | filter.dll / drawer.dll / slideopen.dll / firespark.dll replacements (TJS shims + native pixel helpers) |
| `tools/` | `build-switch.sh`, `build-desktop.sh`, `run-linux.sh`, `ci-report.sh`, `resolve-crash.py`, `xp3ls.cpp` |

Engine hook points (all marked `kk4s:`): `sdl2/stubs.cpp` (main, window, logs, default font), `base/win32/SysInitImpl.cpp` (project dir from the launcher), `base/ScriptMgnIntf.cpp` (before/after startup.tjs, after every executed script), `base/win32/PluginImpl.cpp` (Plugins.link), `utils/DebugIntf.cpp` (TVPAddLog), `visual/FreeType.cpp` (font lookup + fallback), `tjs2/tjsInterCodeExec.cpp` (per-thread register stack), `base/android/StorageImpl.cpp` (relative storage names).

## Set up on a new PC (Windows, no admin needed)

1. `git clone https://github.com/navik677/kk4s` (push needs the navik677 account; if Git Credential Manager has another account cached, use the remote URL `https://navik677@github.com/navik677/kk4s.git`).
2. Portable MSYS2: download `msys2-base-x86_64-latest.sfx.exe` from github.com/msys2/msys2-installer (release `nightly-x86_64`), run it with `-y -o"C:\Users\<you>\"`.
3. In a shell with `MSYSTEM=UCRT64`: `C:\Users\<you>\msys64\usr\bin\bash.exe -lc "pacman -Syu --noconfirm"` (twice), then from the repo: `DEPS=1 bash tools/build-desktop.sh` (installs gcc, cmake, ninja, SDL2, FFmpeg, … and builds).
4. Incremental rebuild (~10–30 s): `cmake --build build-desktop -j8 && cp build-desktop/kk4s.exe out-desktop/`
5. Run (DLLs come from MSYS2):
   ```bash
   PATH=/c/Users/<you>/msys64/ucrt64/bin:$PATH KK4S_ROOT=C:/tmp/kk4s-run ./out-desktop/kk4s.exe "C:/path/to/game/fsn"
   ```
   Log: `$KK4S_ROOT/logs/kk4s-*.log` (engine log, script errors, compat decisions, fonts). Without `KK4S_ROOT` the data folder is next to the exe.

Linux: `DEPS=1 ASAN=1 bash tools/build-desktop.sh`, then `tools/run-linux.sh <game_dir>`. Fedora packages (DEPS only knows apt/pacman): `sudo dnf install SDL2-devel freetype-devel ffmpeg-free-devel libpng-devel libjpeg-turbo-devel turbojpeg-devel libwebp-devel libogg-devel libvorbis-devel opus-devel opusfile-devel zlib-devel oniguruma-devel`.

## Debugging toolbox

- **Log first.** Every script exception is in the log with a VM dump and a TJS stack trace. `compat:` lines show what kk4s did (filters, helper scripts, plugins), `fonts:` lines show fonts and fallbacks.
- **`KK4S_SKIP=AfterInit2.tjs,Override2.tjs`** disables Kirikiroid helper scripts (A/B test).
- **`tools/xp3ls`**: `g++ -O2 -o xp3ls tools/xp3ls.cpp -lz`, then `xp3ls archive.xp3 [filter]` lists files (works on encrypted archives, the index is plain).
- **Probe technique** (read decrypted game scripts without touching the game): make a scratch folder with only a `startup.tjs` (save it as **UTF-16LE with BOM**, otherwise it is read as Shift_JIS) that does
  `Scripts.execStorage("file://./c:/.../fsn/xp3filter.tjs"); var a = [].load("file://./c:/.../fsn/rupatch.xp3>Scripts/Override.tjs"); a.save("file://./c:/tmp/out.tjs"); System.exit();`
  and run kk4s on that folder. Never commit dumped game files.
- **Render probe** (Linux, no screen capture needed): a scratch `startup.tjs` that makes `new Window()`, a `new Layer(w, null)`, sets `font.face`, `drawText(...)`, then `saveLayerImage("/abs/path/out.bmp", "bmp")` and `System.exit()`. Used to check the Cyrillic font fix. Screen capture of the OpenGL window does not work on KDE Wayland (`import` gives black, `spectacle -b` saves nothing).
- **Screenshots for checking a run**: PowerShell `System.Drawing` `CopyFromScreen` on the game window, `SendKeys "{ENTER}"` to advance text (used in this session).
- **CI results without the `gh` CLI**: the public run page `https://github.com/navik677/kk4s/actions/runs/<id>` shows the annotations; `tools/ci-report.sh` puts the first compiler error context there. The REST API has a 60 req/h limit without login.
- Switch crashes: `docs/debugging.md` (`tools/resolve-crash.py` + the `.elf` from the same build).

## Gotchas learned the hard way

- Many engine files use **CRLF**. `sed -i` on them can silently convert the whole file to LF (huge diff). Use Edit, or perl that converts to LF, edits, and converts back. Check with `file <path>` and `git diff --stat`.
- Perl replacement strings eat backslashes (`'\\'` → `'\'`) and `\x..` escapes — re-check literals after a perl edit.
- TJS runs on non-main threads only through the xp3 filter; keep native code that calls TJS on the main thread whenever possible.
- `rm -rf $VAR/...` is blocked by a safety check; use literal paths or `"${VAR:?}"/...`.
- The user wants **code reviewed before running CI checks**, not trial-and-error pushes.

## Next steps (in priority order)

1. **Check sound** by ear (BGM/SE/voice from encrypted xp3) and **video** (`video.xp3`, krmovie path).
2. **Commit the CI state**: check that the last pushes are green for switch + desktop (Linux ASan may find real bugs — fix them).
3. **Real extrans transitions** (wave, ripple, mosaic, turn, rotatezoom/vanish/swap) instead of crossfade.
4. **Native decryptor support**: built-in x86-32 interpreter + minimal PE loader to run a game's own `.tpm`/DLL decrypt routine (decided with the user: own interpreter, not Unicorn — Switch has no JIT for homebrew). This makes encrypted games work without `xp3filter.tjs`.
5. **Saves**: redirect `savedata/` to `<data>/saves/<game_id>/` (M2), overlay folder, game id + profiles (M3).
6. **Switch hardware**: games run on a real Switch (Sharin no Kuni, title takeover). Touch works. Gamepad: buttons reach the engine (`input: pad button` lines in the log) but the game did not react in the first test — check with `tests/tjs/input` (copy to `games/kk4s-input-test/`, it shows every key event on screen) and the `input:` log lines. Freezes: fsync per log line was removed; check `slow frame` lines.
7. More plugins (layerExDraw, layerExImage, kagexopt, …) as other games need them — test more games, the log lists missing plugins/transitions/fonts.
8. Synthetic tests for the compat layer (needs an xp3 packer tool for tests).

## Local-only things on the old PC (not in git)

- `game/fsn/` — the test game (copy your own).
- `krkrs-master/`, `kirikiri-switch-prompt.md` — originals, already in the repo as `engine/` and `docs/original-prompt.md`.
- MSYS2 in `C:\Users\asir214\msys64`.
