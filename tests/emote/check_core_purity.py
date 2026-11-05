#!/usr/bin/env python3
"""Guard: no game-specific logic outside compat/emote/profiles/.

  python3 tests/emote/check_core_purity.py

Scans the code of compat/emote (comments stripped) for game names, key/seed
constants, character-specific variable names, file-name tests and non-ASCII
literals (real files use Japanese layer/motion names). Build-specific data
belongs in compat/emote/profiles/ only. Exit 0 when clean.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "compat", "emote"))
ALLOWED_DIR = os.path.join(ROOT, "profiles")

FORBIDDEN = [
    (r"(?i)neko", "game name"),
    (r"(?i)atri|fate|stay.?night", "game name"),
    (r"742877301|(?i:0x2c46c275)", "NekoPara key"),
    (r"123456789|362436069|521288629|(?i:0x075bcd15|0x159a55e5|0x1f123bb5)", "xorshift seeds"),
    (r"face_eye_open|head_LR|body_slant|face_mouth|face_talk", "character variable name"),
    (r"(?i)\.xp3|\.dll|emotewin|dx_", "file/archive name test"),
    (r"(?i)\bgame\s*==|game_name|filename", "game/file switch"),
    (r"[^\x00-\x7f]", "non-ASCII literal"),
]


def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), src, flags=re.S)
    out = []
    for line in src.split("\n"):
        # drop // comments that are not inside a string literal
        in_str = False
        cut = len(line)
        i = 0
        while i < len(line):
            c = line[i]
            if c == "\\" and in_str:
                i += 2
                continue
            if c == '"':
                in_str = not in_str
            elif not in_str and line.startswith("//", i):
                cut = i
                break
            i += 1
        out.append(line[:cut])
    return "\n".join(out)


def main():
    bad = []
    files = 0
    for dirpath, _, names in os.walk(ROOT):
        if os.path.abspath(dirpath).startswith(ALLOWED_DIR):
            continue
        for n in names:
            if not n.endswith((".h", ".cpp")):
                continue
            files += 1
            p = os.path.join(dirpath, n)
            with open(p, encoding="utf-8") as f:
                code = strip_comments(f.read())
            for ln, line in enumerate(code.split("\n"), 1):
                for pat, what in FORBIDDEN:
                    if re.search(pat, line):
                        bad.append("%s:%d: %s: %s" % (os.path.relpath(p, ROOT), ln, what, line.strip()[:100]))
    for b in bad:
        print("FAIL", b)
    print("core purity: %d files checked, %d findings" % (files, len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
