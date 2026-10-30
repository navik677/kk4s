# Debugging

## Logs
- Every run writes `sdmc:/switch/kk4s/logs/kk4s-<date>-<time>.log`. Each line is flushed and `fsync`ed, so the last line before a crash is on disk.
- First lines: version, git hash, nxlink state, memory total/used, applet vs title-takeover mode.

## nxlink (logs over Wi-Fi, no SD swapping)
1. On Switch: hbmenu, press Y (netloader).
2. On PC (devkitPro installed): `nxlink -s out/kk4s.nro`
   `-s` keeps the connection open and prints stdout/stderr on the PC.

## Atmosphere crash reports
1. After a crash, copy `sdmc:/atmosphere/crash_reports/<id>.log` to the PC.
2. Take `kk4s.elf` **from the same build** as the `.nro` (CI artifact or nightly release, check the git hash in the kk4s log).
3. Run:
   ```bash
   python3 tools/resolve-crash.py crash.log kk4s.elf
   ```
   It finds the kk4s module base and resolves the backtrace with `aarch64-none-elf-addr2line`.
   By hand: `aarch64-none-elf-addr2line -f -C -e kk4s.elf 0x<pc - module_base>`.

## Dev mode on hardware
- Launch hbmenu in **title takeover** (hold R while starting a game). Album (applet) mode has much less memory; test it separately (checklist).
