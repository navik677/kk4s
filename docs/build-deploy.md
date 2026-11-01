# Build & deploy (the commands that work on this dev box)

This is the exact procedure used on 2026-11-01 (Windows, MSYS2 + devkitPro).
Repo: `C:\Users\navik\gits\kk4s`; MSYS2: `C:\msys64`.

## Switch build (NRO)

```powershell
C:\msys64\usr\bin\bash.exe -lc "cd /c/Users/navik/gits/kk4s && SKIP_DEPS=1 bash tools/build-switch.sh > /tmp/kk4s-switch-build.log 2>&1; echo EXIT=$?; grep -n -i -E 'error:|undefined reference' /tmp/kk4s-switch-build.log | tail -10; tail -4 /tmp/kk4s-switch-build.log"
```

- Works because devkitPro is installed at `/opt/devkitpro` (MSYS path) and the
  build script defaults `DEVKITPRO=/opt/devkitpro`; the toolchain cmake is
  `/opt/devkitpro/portlibs/switch/bin/aarch64-none-elf-cmake`.
- `SKIP_DEPS=1` is required on this machine: the script's dependency installer
  uses `dkp-pacman`, which is not on PATH here. The portlibs are already
  installed (SDL2, FFmpeg, FreeType, libpng/jpeg/webp, ogg/vorbis/opus/opusfile,
  zlib, curl, oniguruma).
- Output: `out/kk4s.nro` and `out/kk4s.elf` (the ELF is for
  `tools/resolve-crash.py`).
- The build is incremental in `build-switch/`. For a clean build:
  `rm -rf build-switch` (in bash) and run again.
- The git hash in the banner is baked at configure time. Commit first, then
  build (or re-run the configure) if the banner should show the new hash.
- Log: `/tmp/kk4s-switch-build.log` inside MSYS (`C:\msys64\tmp\...`).

Checks when it fails:
- `ls /opt/devkitpro/portlibs/switch/bin/aarch64-none-elf-cmake` must exist.
- `/opt/devkitpro/devkitA64/bin/aarch64-none-elf-as.exe --version` must run.
- Windows **Smart App Control / antivirus** can block devkitA64 executables
  (seen before: the assembler was blocked; reinstalling `devkita64-gcc` fixed
  it, the old binary was renamed `*.blocked`). If the build dies with an
  access-denied/"app cannot run" error at an assembly step, check this first.

## Desktop viewer (PC test loop)

Dependencies (UCRT64, once):

```powershell
C:\msys64\usr\bin\bash.exe -lc "pacman -S --noconfirm --needed \
 mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
 mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-freetype \
 mingw-w64-ucrt-x86_64-ffmpeg mingw-w64-ucrt-x86_64-libpng mingw-w64-ucrt-x86_64-libjpeg-turbo \
 mingw-w64-ucrt-x86_64-libwebp mingw-w64-ucrt-x86_64-libogg mingw-w64-ucrt-x86_64-libvorbis \
 mingw-w64-ucrt-x86_64-opus mingw-w64-ucrt-x86_64-opusfile mingw-w64-ucrt-x86_64-zlib \
 mingw-w64-ucrt-x86_64-oniguruma mingw-w64-ucrt-x86_64-curl"
```

Build:

```powershell
C:\msys64\usr\bin\bash.exe -lc "export MSYSTEM=UCRT64; export PATH=/ucrt64/bin:/usr/bin:`$PATH; cd /c/Users/navik/gits/kk4s && bash tools/build-desktop.sh > /tmp/kk4s-desktop-build.log 2>&1; echo EXIT=`$?; tail -20 /tmp/kk4s-desktop-build.log"
```

- Use the **UCRT64** cmake (`/ucrt64/bin/cmake.exe`), not the MSYS
  (`/usr/bin/cmake.exe`): the MSYS one emits POSIX include paths and the build
  fails with `SDL.h: No such file or directory`.
- `tools/build-desktop.sh` copies `build-desktop/kk4s.exe` to `out-desktop/`.
  An incremental `cmake --build build-desktop` does **not** copy; copy it
  yourself:
  `Copy-Item build-desktop\kk4s.exe out-desktop\kk4s.exe -Force`.
- Running needs the UCRT64 DLLs on `PATH`, otherwise the process exits with
  `0xC0000135` (DLL not found):

```powershell
$env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH
$env:KK4S_ROOT = 'E:\kk4s-pc'          # data folder: logs/, saves/, shots/
Start-Process 'C:\Users\navik\gits\kk4s\out-desktop\kk4s.exe' -ArgumentList '"E:\ATRI -My Dear Moments-"'
```

- Quote game paths with spaces (otherwise `Start-Process` splits the argument
  and the viewer opens the launcher with 0 games).
- Logs: `E:\kk4s-pc\logs\kk4s-*.log`.
- `KK4S_INPUT_SCRIPT` screenshots are black with the Direct3D renderer; use a
  whole-screen capture (PowerShell + `System.Drawing.CopyFromScreen`) for visual
  checks.
- gdb (UCRT64) for crashes:
  `C:\msys64\ucrt64\bin\gdb.exe -batch -ex run -ex "bt 25" --args out-desktop\kk4s.exe "E:\ATRI -My Dear Moments-"`.

## Deploy over FTP (Switch)

The user's Switch runs ftpd (anonymous, port 5000, e.g. `192.168.1.135`).

```powershell
curl.exe -sS --max-time 1200 -T out\kk4s.nro ftp://192.168.1.135:5000/switch/kk4s/kk4s.nro
```

Verify the upload (hash must match):

```powershell
$tmp = "$env:TEMP\remote-kk4s.nro"
curl.exe -sS --max-time 600 -o $tmp ftp://192.168.1.135:5000/switch/kk4s/kk4s.nro
(Get-FileHash out\kk4s.nro -Algorithm SHA256).Hash
(Get-FileHash $tmp -Algorithm SHA256).Hash
```

- NRO path on the SD: `/switch/kk4s/kk4s.nro`; games in `/switch/kk4s/games/`;
  logs in `/switch/kk4s/logs/`.
- List logs: `curl.exe -s ftp://192.168.1.135:5000/switch/kk4s/logs/`.
  The current log cannot be downloaded while the game is running (file open) —
  quit the game first.
- If curl exits with `28` (timeout), the console is asleep/off; wake it and
  retry. FTP stops when the console sleeps.
- Upload one big file per `curl` call and compare hashes; the SD is slow.
- After this session the SD holds an NRO built from `main` `4d767de` (with
  `System.addFont`), SHA-256 `6D75B284969C02270EF55162DD6196F1F5C542F4AA2185DB0BD477459A608383`.
  The native WIP branch (`wip/packinone-native`) is **not** in that NRO.

## Alternative: CI / Docker

`tools/build-switch.sh` is the same script CI and the Docker image use:
`gh run download <id>` for the CI artifact, or
`podman run --rm -v "$PWD":/src:Z kk4s-build bash tools/build-switch.sh`
(see `README.md` and `docs/HANDOFF.md`). These paths install deps themselves and
do not need the local MSYS2 workarounds above.
