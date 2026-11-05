#!/usr/bin/env python3
"""List and compare the native (C++) TJS API of Kirikiri engine source trees.

    tools/native-api.py list  <tree>                 # class -> members of one tree
    tools/native-api.py diff  <tree> <reference>...  # members the first tree lacks
    tools/native-api.py diff  engine/source/core ~/krkrz/common ~/krkrz/win32 \\
                              ~/krkr2/kirikiri2/trunk/kirikiri2/src/core --markdown

A member is anything declared inside TJS_BEGIN_NATIVE_MEMBERS(Class) ...
TJS_END_NATIVE_MEMBERS with TJS_BEGIN_NATIVE_METHOD_DECL / _PROP_DECL (the way
Kirikiri 2, Kirikiri Z and krkrs register built-in classes). Members added at
run time (ncbind plugins, TJS scripts, kk4s compat shims) are not seen, so a
"missing" member may still exist in kk4s through compat/ - check before
implementing. Comments inside the macro arguments are ignored.
"""
import re
import sys
from collections import defaultdict
from pathlib import Path

MEMBERS = re.compile(r"TJS_BEGIN_NATIVE_MEMBERS\s*\(\s*(?:/\*.*?\*/\s*)?(\w+)\s*\)")
END = re.compile(r"TJS_END_NATIVE_MEMBERS")
DECL = re.compile(r"TJS_BEGIN_NATIVE_(METHOD|PROP)_DECL\s*\(\s*(?:/\*.*?\*/\s*)?(\w+)\s*\)")


def scan(trees: list[str]) -> dict[str, dict[str, str]]:
    """class -> {member: 'method'|'prop'} over all .cpp files below the trees."""
    api: dict[str, dict[str, str]] = defaultdict(dict)
    for tree in trees:
        for path in sorted(Path(tree).rglob("*.cpp")):
            try:
                text = path.read_bytes().decode("latin-1")
            except OSError:
                continue
            pos = 0
            while True:
                m = MEMBERS.search(text, pos)
                if not m:
                    break
                end = END.search(text, m.end())
                stop = end.start() if end else len(text)
                for d in DECL.finditer(text, m.end(), stop):
                    api[m.group(1)].setdefault(d.group(2), d.group(1).lower())
                pos = stop
    return api


def main() -> int:
    args = sys.argv[1:]
    markdown = "--markdown" in args
    args = [a for a in args if a != "--markdown"]
    if len(args) >= 2 and args[0] == "list":
        api = scan(args[1:])
        for cls in sorted(api):
            print(f"{cls}: {' '.join(sorted(api[cls]))}")
        return 0
    if len(args) >= 3 and args[0] == "diff":
        ours, ref = scan([args[1]]), scan(args[2:])
        rows = []
        for cls in sorted(ref):
            missing = sorted(set(ref[cls]) - set(ours.get(cls, {})))
            if cls not in ours:
                rows.append((cls, "(class missing)", missing))
            elif missing:
                rows.append((cls, "", missing))
        if markdown:
            print("| Class | Members missing here |")
            print("|---|---|")
            for cls, note, missing in rows:
                members = ", ".join(f"`{m}`" for m in missing)
                print(f"| {cls} {note} | {members} |")
        else:
            for cls, note, missing in rows:
                print(f"{cls} {note}: {' '.join(missing)}")
        return 0
    print(__doc__.strip(), file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
