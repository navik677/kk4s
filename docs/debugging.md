# Debugging

## Logs

- Every run writes `sdmc:/switch/kk4s/logs/kk4s-<date>-<time>.log` (PC: `<data>/logs/`).
- First lines: version, git hash, data folder, memory total/used, applet vs title-takeover mode,
  renderer.
- The log is written by a background thread every 200 ms (fsync every 2 s, flushed at exit).
  Writing each line directly cost several ms per line on the SD card and froze games. On a crash the
  last ~200 ms of lines can be missing — the Atmosphere crash report has the backtrace.
- Useful prefixes: `compat:` (filters, helper scripts, plugins, saves), `fonts:`, `input:`,
  `launcher:`, `overlay:`, `slow frame: N ms (...)` (frames over 200 ms, with where the time went:
  `compose` = software layer drawing, `upload` = frame → GPU texture, `present` = vsync wait,
  `file reads` = whole archive files read + decrypted on the main thread (ms / count),
  `writes` = local files written (saves), `gc` = TJS garbage collection / rehash, `other` = the rest,
  mostly game scripts). `threads:` / `perf:` show where worker threads went and the CPU boost.
- PC aids: `KK4S_SLOW_MS=<n>` lowers the slow-frame threshold (the PC is ~10–20× faster than the
  Switch, so 10–15 ms on PC ≈ a visible stall on the console); `KK4S_LOG_READS=1` logs every archive
  file read on the main thread.
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

## Compiled TJS2 scripts (`tools/tjsdis.py`)

A `startup.tjs` (or any `.tjs`/`.scn`) can be TJS2 **bytecode** instead of text. If a game quits
before KAG starts, disassemble it:

```bash
python tools/tjsdis.py path/to/startup.tjs
```

It prints the data-area strings and the VM instructions (opcodes are from
`engine/source/core/tjs2/tjsInterCodeGen.h`). A repack protection stub looks like a single
`System.inform("...")` call with no branches; see the ATRI section in `docs/HANDOFF.md`.

## Input scripts (PC)

`KK4S_INPUT_SCRIPT=<file>` plays gamepad presses, touches and screenshots through the same code as
the Switch pad (`sdl2/stubs.cpp`, `kk4sScriptTick`): lines `<ms> pad A [hold_ms]`, `<ms> tap x y`
(0..1), `<ms> shot name.bmp` (to `$KK4S_ROOT/shots/`), `<ms> eval <TJS expression>` (result in the
log, e.g. `kag.mainConductor.curStorage + ":" + kag.mainConductor.curLine`), `<ms> quit`; `+<ms>` is
relative.
