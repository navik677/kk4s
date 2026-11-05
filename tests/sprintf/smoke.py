#!/usr/bin/env python3
"""TJS string formatting against C printf.

Usage: python tests/sprintf/smoke.py path/to/kk4s

String.sprintf and the real -> string conversion go through kk4s' own
TJS_snprintf (engine/source/core/tjs2/tjsSnprintf.cpp; it once printed %g as
%f). This writes a startup.tjs that formats each case and compares the result
with Python's % operator, which follows C printf (the C99 / glibc output that
Kirikiri's runtime also produces for these conversions: two-digit exponents).
Real -> string is "%.15g" with NaN / +Infinity / -Infinity / +0.0 / -0.0, as
TJS does. Cases Python formats differently from C (%#o, negative %u / %x / %o)
have their C result written out.
"""
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

# (TJS format, TJS value literal, Python value)
CASES = [
    ("%d", "0", 0), ("%d", "-1", -1), ("%d", "2147483647", 2147483647), ("%d", "-2147483648", -2147483648),
    ("%d", "9223372036854775807", 9223372036854775807), ("%d", "-9223372036854775807", -9223372036854775807),
    ("%5d", "42", 42), ("%-5d|", "42", 42), ("%05d", "-42", -42), ("%+d", "5", 5), ("%+d", "-5", -5),
    ("%x", "255", 255), ("%X", "48879", 48879), ("%08x", "3054", 3054), ("%#x", "255", 255),
    ("%o", "8", 8), ("%.3d", "7", 7), ("%10.4d", "42", 42), ("%i", "123", 123),
    ("%u", "4000000000", 4000000000),
    ("%f", "1.5", 1.5), ("%.2f", "3.14159", 3.14159), ("%10.3f", "2.71828", 2.71828),
    ("%-10.3f|", "2.71828", 2.71828), ("%+.1f", "2.25", 2.25), ("%.0f", "0.5", 0.5), ("%.0f", "1.5", 1.5),
    ("%.0f", "2.5", 2.5), ("%f", "1e20", 1e20), ("%.10f", "1/3", 1 / 3), ("%f", "-0.0", -0.0),
    ("%f", "123456789.125", 123456789.125), ("%.3f", "-0.0005", -0.0005),
    ("%e", "12345.678", 12345.678), ("%.3e", "0.000123", 0.000123), ("%E", "1e300", 1e300),
    ("%e", "0", 0.0), ("%e", "-1e-300", -1e-300), ("%.0e", "15000", 15000.0), ("%#.0e", "15000", 15000.0),
    ("%g", "0.0001", 0.0001), ("%g", "0.00001", 0.00001), ("%g", "100000", 100000.0), ("%g", "1000000", 1000000.0),
    ("%.3g", "3.14159", 3.14159), ("%G", "1e-10", 1e-10), ("%g", "0", 0.0), ("%#g", "1", 1.0),
    ("%.15g", "0.1", 0.1), ("%.17g", "0.1", 0.1), ("%g", "1e100", 1e100), ("%g", "123456", 123456.0),
    ("%g", "1234567", 1234567.0), ("%.10g", "1/3", 1 / 3), ("%g", "-2.5", -2.5), ("%12g|", "0.5", 0.5),
    ("%-12g|", "0.5", 0.5), ("%+g", "7", 7.0), ("%.0g", "123", 123.0), ("%.20g", "1/3", 1 / 3),
    ("%g", "9.9999995e-5", 9.9999995e-5), ("%g", "999999.5", 999999.5), ("%f", "0.000001", 0.000001),
    ("%s", '"abc"', "abc"), ("%5s", '"abc"', "abc"), ("%-5s|", '"abc"', "abc"), ("%.2s", '"abcdef"', "abcdef"),
    ("[%s]", '"日本語"', "日本語"), ("%%", None, None), ("%d%%", "50", 50),
]

# C printf results that Python's % does not produce (it writes 0o10, and
# formats negative values as signed): (TJS format, TJS value literal, expected)
EXPLICIT = [
    ("%#o", "8", "010"), ("%#o", "0", "0"), ("%#x", "0", "0"), ("%#X", "255", "0XFF"),
    ("%#10x|", "255", "      0xff|"), ("%#8x|", "255", "    0xff|"),
    # TJS takes one flag only (Kirikiri 2 does the same): "%-#" stops at "#"
    ("%-#8x|", "255", "#8x|"), ("%#08x", "255", "0x0000ff"),
    ("%u", "-1", "18446744073709551615"), ("%x", "-1", "ffffffffffffffff"), ("%o", "-1", "1777777777777777777777"),
    ("%d", "(-9223372036854775807-1)", "-9223372036854775808"), ("%20d|", "-1", "                  -1|"),
]

REALS = ["0.0", "2.5", "0.1", "1e20", "1e-7", "123456789012345.0", "1234567890123456.0", "1/3", "-0.0", "1e300",
         "Math.PI", "100.0", "0.5", "-1.25e-10", "1e15", "1e16", "0.0001", "0.00001", "NaN", "Infinity", "-Infinity"]


def expected_real(v: float) -> str:
    if v == 0.0:  # TJS (TJSSpecialRealToString), not printf
        return "-0.0" if math.copysign(1.0, v) < 0 else "+0.0"
    if math.isnan(v):
        return "NaN"
    if math.isinf(v):
        return "+Infinity" if v > 0 else "-Infinity"
    return "%.15g" % v


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    executable = str(Path(sys.argv[1]).resolve())
    lines = []
    lines.append("function out(i, s) { Debug.message(\"FMT \" + i + \" [\" + s + \"]\"); }\n")
    want = {}
    for i, (fmt, lit, val) in enumerate(CASES):
        tjs_fmt = fmt.replace('"', '\\"')
        lines.append(f'out({i}, "{tjs_fmt}".sprintf({lit}));\n' if lit is not None else f'out({i}, "{tjs_fmt}".sprintf());\n')
        want[i] = fmt % val if lit is not None else fmt % ()
    for j, (fmt, lit, exp) in enumerate(EXPLICIT):
        i = 500 + j
        lines.append(f'out({i}, "{fmt}".sprintf({lit}));\n')
        want[i] = exp
    for j, lit in enumerate(REALS):
        i = 1000 + j
        tjs = {"NaN": "(0.0/0.0)", "Infinity": "(1.0/0.0)", "-Infinity": "(-1.0/0.0)"}.get(lit, lit)
        lines.append(f'out({i}, (string)(real)({tjs}));\n')
        py = {"NaN": "float('nan')", "Infinity": "float('inf')", "-Infinity": "float('-inf')",
              "Math.PI": "math.pi"}.get(lit, lit)
        want[i] = expected_real(float(eval(py)))
    lines.append('Debug.message("FMT done");\n')
    with tempfile.TemporaryDirectory(prefix="kk4s-sprintf-") as temp:
        base = Path(temp)
        game = base / "game"
        game.mkdir()
        (game / "startup.tjs").write_text("".join(lines), encoding="utf-8-sig")
        root = base / "root"
        script = base / "input.txt"
        script.write_text("2500 quit\n", encoding="ascii")
        env = os.environ.copy()
        env["KK4S_ROOT"] = str(root)
        env["KK4S_INPUT_SCRIPT"] = str(script)
        subprocess.run([executable, str(game)], env=env, timeout=60, check=False, capture_output=True)
        logs = sorted((root / "logs").glob("kk4s-*.log"))
        log = logs[-1].read_text(encoding="utf-8", errors="replace") if logs else ""
    got = {int(m.group(1)): m.group(2) for m in re.finditer(r"FMT (\d+) \[(.*)\]$", log, re.M)}
    problems = []
    if "FMT done" not in log:
        problems.append("the script did not finish:\n" + "\n".join(l for l in log.splitlines() if "xception" in l or "rror" in l)[-2000:])
    for i, w in want.items():
        what = (CASES[i][0] + " " + str(CASES[i][1]) if i < 500 else
                EXPLICIT[i - 500][0] + " " + EXPLICIT[i - 500][1] if i < 1000 else "(string)" + REALS[i - 1000])
        if got.get(i) != w:
            problems.append(f"{what}: got {got.get(i)!r}, want {w!r}")
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    print(f"TJS sprintf / real -> string vs printf: PASS ({len(want)} cases)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
