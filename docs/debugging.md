# Debugging

## Logs

- Every run writes `sdmc:/switch/kk4s/logs/kk4s-<date>-<time>.log` (PC: `<data>/logs/`).
- First lines: version, git hash, data folder, memory total/used, applet vs title-takeover mode,
  renderer.
- The log is written by a background thread every 200 ms (fsync every 2 s, flushed at exit).
  Writing each line directly cost several ms per line on the SD card and froze games. On a crash the
  last ~200 ms of lines can be missing — the Atmosphere crash report has the backtrace.
- Useful prefixes: `compat:` (filters, helper scripts, plugins, saves), `fonts:`, `input:`,
  `launcher:`, `overlay:`, `slow frame: N ms` (frames over 200 ms).
- A log of a running game cannot be read over FTP (the file is open): quit the game first.

## nxlink (stdout over Wi-Fi)

1. On Switch: hbmenu, press Y (netloader).
2. On PC (devkitPro installed): `nxlink -s out/kk4s.nro` — `-s` keeps the connection open and
   prints stdout/stderr.

## Atmosphere crash reports

1. Copy `sdmc:/atmosphere/crash_reports/<time>_<program id>.log` (FTP works).
2. Take `kk4s.elf` **from the same build** as the `.nro` (CI artifact; check the git hash in the
   kk4s log).
3. Resolve:
   ```bash
   python3 tools/resolve-crash.py crash.log kk4s.elf          # needs aarch64-none-elf-addr2line
   eu-addr2line -f -C -i -e kk4s.elf 0x111a08                 # elfutils works too: offsets are "kk4s + 0x..."
   ```
4. `Fault Address 0x0` with a small `Address` (e.g. `0x1c`) usually means a NULL object pointer:
   the address is the field offset.

## Dev mode on hardware

- Launch hbmenu in **title takeover** (hold R while starting a game). Album (applet) mode has much
  less memory.
- `tests/tjs/input` (copy to `games/kk4s-input-test/`) shows every key / mouse event the game gets.
