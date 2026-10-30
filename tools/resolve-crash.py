#!/usr/bin/env python3
"""Symbolize an Atmosphere crash report against kk4s.elf.

usage: resolve-crash.py <crash_report.log> <kk4s.elf> [--addr2line PATH]

Atmosphere reports list modules with their start addresses and a backtrace
of absolute addresses. We find the module that contains each address
(the kk4s main module is the one whose name ends in .nro / "main" or the
first non-system module), subtract its base and run addr2line.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

HEX = r"0x[0-9a-fA-F]+|[0-9a-fA-F]{16}"


def parse_modules(text):
    """Return list of (start, end, name)."""
    mods = []
    # Typical line: "    Module 0:  0000000008000000-0000000008001000  ... name"
    for m in re.finditer(r"(?im)^\s*(?:Module\s*\d+:?|\[\d+\])?\s*(?P<s>" + HEX + r")\s*-\s*(?P<e>" + HEX + r")\s*(?P<rest>.*)$", text):
        s = int(m.group("s"), 16)
        e = int(m.group("e"), 16)
        if e > s:
            mods.append((s, e, m.group("rest").strip()))
    return mods


def parse_addresses(text):
    addrs = []
    section = re.search(r"(?is)(backtrace|stack trace).*", text)
    body = section.group(0) if section else text
    for m in re.finditer(r"(?im)^\s*(?:ReturnAddress\[\d+\]|\[\d+\]|#\d+|PC|LR)?[:\s]*(" + HEX + r")", body):
        addrs.append(int(m.group(1), 16))
    # PC / LR registers first if present
    for reg in ("PC", "LR"):
        r = re.search(r"(?im)^\s*" + reg + r"\s*[:=]\s*(" + HEX + r")", text)
        if r:
            addrs.insert(0, int(r.group(1), 16))
    seen, out = set(), []
    for a in addrs:
        if a not in seen and a > 0x1000:
            seen.add(a)
            out.append(a)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("report")
    ap.add_argument("elf")
    ap.add_argument("--addr2line", default=None)
    args = ap.parse_args()

    a2l = args.addr2line or shutil.which("aarch64-none-elf-addr2line")
    if not a2l:
        dkp = os.environ.get("DEVKITPRO", "/opt/devkitpro")
        cand = os.path.join(dkp, "devkitA64", "bin", "aarch64-none-elf-addr2line")
        a2l = cand if os.path.exists(cand) else None
    if not a2l:
        sys.exit("aarch64-none-elf-addr2line not found (install devkitA64 or pass --addr2line)")

    text = open(args.report, encoding="utf-8", errors="replace").read()
    mods = parse_modules(text)
    addrs = parse_addresses(text)
    if not addrs:
        sys.exit("no addresses found in report")

    main_mod = None
    for s, e, name in mods:
        if re.search(r"(?i)kk4s|\.nro|main", name):
            main_mod = (s, e, name)
            break
    if main_mod is None and mods:
        main_mod = mods[0]
    if main_mod is None:
        sys.exit("no module list found in report")
    base = main_mod[0]
    print(f"main module: {main_mod[2] or '?'} base=0x{base:x}")

    for a in addrs:
        inside = [m for m in mods if m[0] <= a < m[1]]
        if inside and inside[0][0] != base:
            print(f"0x{a:016x}  (in {inside[0][2] or 'other module'})")
            continue
        off = a - base
        res = subprocess.run([a2l, "-f", "-C", "-i", "-e", args.elf, hex(off)],
                             capture_output=True, text=True)
        lines = [l for l in res.stdout.splitlines() if l.strip()]
        sym = " | ".join(lines) if lines else "??"
        print(f"0x{a:016x}  +0x{off:x}  {sym}")


if __name__ == "__main__":
    main()
