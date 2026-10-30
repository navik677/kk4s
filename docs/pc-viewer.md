# PC viewer

Same engine as the Switch build, with `platform/desktop/` instead of libnx.
Use it as the main dev/test loop: crashes here are much cheaper than on the Switch.

## Run
```
kk4s <path to game folder>     # run one game directly (drag & drop a folder onto kk4s.exe works too)
kk4s                           # pick from <data folder>/games/ (dialog with buttons, or a list in a terminal)
```
Data folder (games/, logs/, saves/, ...) = folder of the executable, or `$KK4S_ROOT`.
Window: 1280x720, resizable.

## Build
- Linux: `DEPS=1 bash tools/build-desktop.sh` (apt deps), `ASAN=1` for AddressSanitizer + UBSan.
  Run: `tools/run-linux.sh <game_dir>`.
- Windows: in MSYS2 UCRT64 shell: `DEPS=1 bash tools/build-desktop.sh`.
- CI (`.github/workflows/desktop.yml`) builds both; download from the Actions run artifacts:
  `kk4s-linux-<sha>` and `kk4s-windows-<sha>` (exe + DLLs).

## Status
- Linux: primary target, built with sanitizers in CI, smoke test runs `tests/tjs/hello` headless.
- Windows: **experimental**. Engine preprocessor checks for `_WIN32` / `WIN32` / `__WIN32__` were renamed to
  `KK4S_WIN32_NATIVE` (never defined), so MinGW builds the same SDL/POSIX code path as Linux and Switch
  instead of the MSVC + Win32 API branches. Differences left: 16-bit `wchar_t`, Windows paths.
