#!/usr/bin/env python3
"""KAGParserEx.dll: the extended KAG parser replaces KAGParser when linked.

Usage: python tests/kagparserex/smoke.py path/to/kk4s

Boots tests/tjs/kagparserex/startup.tjs, which parses test.ks with the linked
parser and logs every tag. Checked against the plugin's readme / manual:
- multiLineTagEnabled: "@multi a=1 \\" + ";   b=2 \\" + ";   c=3" is one tag
- [pmacro name=pm x=10 y=20] registers a parameter macro; "@usepm pm" gets
  x=10 y=20 in place of "pm"; pmacro itself is consumed by the parser
- "taglist" lists "tagname" and then the attribute names in the order they
  were written (upstream also leaves empty entries for the " \" breaks of a
  multi-line tag; only the order of the names is checked there)
- macro "*": in "[inner p=1 *]" the parameter written before "*" is kept
  (p=1 as well as the caller's q=2)
- [emb ... escape=false] evaluates the text as a tag ([nested k=v])
- hasLabel() is true / false without throwing
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    executable = str(Path(sys.argv[1]).resolve())
    game = Path(__file__).resolve().parents[1] / "tjs" / "kagparserex"
    with tempfile.TemporaryDirectory(prefix="kk4s-kagparserex-") as temp:
        base = Path(temp)
        root = base / "user-data"
        script = base / "input.txt"
        script.write_text("2000 quit\n", encoding="ascii")
        env = os.environ.copy()
        env["KK4S_ROOT"] = str(root)
        env["KK4S_INPUT_SCRIPT"] = str(script)
        result = subprocess.run([executable, str(game)], env=env, timeout=60, check=False,
                                capture_output=True, text=True)
        logs = sorted((root / "logs").glob("kk4s-*.log"))
        log = logs[-1].read_text(encoding="utf-8", errors="replace") if logs else ""
    tags = re.search(r"KAGPX tags (.*)", log)
    labels = re.search(r"KAGPX hasLabel (.*)", log)
    got = tags.group(1).split() if tags else []
    problems = []
    want = {
        "parameter macro": "usepm(x=10,y=20)[tagname,x,y]",
        "taglist order": "tag2(x=3,y=2,z=1)[tagname,z,y,x]",
        "macro * keeps earlier params": "inner(p=1,q=2)",
        "emb escape=false": "nested(k=v)[tagname,k]",
    }
    for what, tag in want.items():
        name = tag.split("(")[0].split("[")[0]
        found = [t for t in got if t.split("(")[0].split("[")[0] == name]
        if not found or not found[0].startswith(tag):
            problems.append(f"{what}: want {tag}, got {found or 'nothing'}")
    multi = [t for t in got if t.startswith("multi")]
    names = [n for n in multi[0].split("[", 1)[1].rstrip("]").split(",") if n] if multi and "[" in multi[0] else []
    if not multi or not multi[0].startswith("multi(a=1,b=2,c=3)") or names != ["tagname", "a", "b", "c"]:
        problems.append(f"multi-line tag: want multi(a=1,b=2,c=3) with names tagname,a,b,c, got {multi or 'nothing'}")
    if any(t.startswith("pmacro") for t in got):
        problems.append("pmacro was returned as a tag (the parser must consume it)")
    if not labels or labels.group(1).strip() != "1 0":
        problems.append(f"hasLabel: {labels.group(1) if labels else 'no result'}, want 1 0")
    if result.returncode != 0:
        problems.append(f"kk4s exited with {result.returncode}")
    if problems:
        print("\n".join(problems), file=sys.stderr)
        print("tags: " + " ".join(got), file=sys.stderr)
        print("\n".join(l for l in log.splitlines() if "KAGPX" in l or "xception" in l)[-3000:], file=sys.stderr)
        return 1
    print("KAGParserEx.dll: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
