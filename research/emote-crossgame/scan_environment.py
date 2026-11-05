#!/usr/bin/env python3
"""Inventory local game binaries and PSB samples for cross-game research.

Read-only: it hashes files and reads headers, nothing else.  Output goes to
``research/emote-crossgame/work/`` (gitignored).  Point it at directories you
are allowed to inspect; the default is the repository's ``game/`` directory.

Usage:
  python3 research/emote-crossgame/scan_environment.py [--root DIR ...]
      [--json-out PATH]
"""
import argparse
import hashlib
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)

from audit_dll import audit_pe  # noqa: E402


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def psb_head(path):
    with open(path, "rb") as f:
        head = f.read(0x40)
    rec = {
        "path": path,
        "size": os.path.getsize(path),
        "sha256": sha256(path),
        "magic": head[:4].hex(),
        "plain_psb": head[:4] == b"PSB\0",
        "magic_xor_key": None,
    }
    if not rec["plain_psb"] and len(head) >= 4:
        k = head[0] ^ 0x50
        if all(head[i] ^ k == b"PSB\0"[i] for i in range(4)):
            rec["magic_xor_key"] = k
    return rec


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", action="append", default=None)
    ap.add_argument("--json-out", default=os.path.join(HERE, "work", "env_scan.json"))
    ap.add_argument("--no-psb", action="store_true")
    args = ap.parse_args(argv)

    roots = args.root or [os.path.join(REPO, "game")]
    pe_files, psb_files, archives = [], [], []
    for root in roots:
        for dirpath, _dirs, names in os.walk(root):
            for name in names:
                path = os.path.join(dirpath, name)
                low = name.lower()
                if low.endswith((".dll", ".exe")):
                    pe_files.append(path)
                elif low.endswith(".psb") and not args.no_psb:
                    psb_files.append(path)
                elif low.endswith(".xp3"):
                    archives.append({"path": path, "size": os.path.getsize(path)})

    report = {
        "roots": roots,
        "binaries": [],
        "psb_samples": [],
        "archives": sorted(archives, key=lambda a: a["path"]),
    }
    for path in pe_files:
        try:
            report["binaries"].append(audit_pe(path))
        except Exception as e:
            report["binaries"].append({"path": path,
                                       "error": "%s: %s" % (type(e).__name__, e)})
    for path in psb_files:
        try:
            report["psb_samples"].append(psb_head(path))
        except Exception as e:
            report["psb_samples"].append({"path": path, "error": str(e)})

    os.makedirs(os.path.dirname(os.path.abspath(args.json_out)), exist_ok=True)
    with open(args.json_out, "w") as f:
        json.dump(report, f, indent=1)

    print("roots:", ", ".join(roots))
    print("binaries: %d, psb samples: %d, xp3 archives: %d"
          % (len(report["binaries"]), len(report["psb_samples"]),
             len(report["archives"])))
    for rec in report["binaries"]:
        if "error" in rec:
            print("  ERROR", rec["path"], rec["error"])
            continue
        if rec["markers"] or rec["constants"]["ascii_key"] or rec["constants"]["u32_key"]:
            print("  %-60s key_text=%s u32=%s markers=%s"
                  % (os.path.relpath(rec["path"], REPO),
                     bool(rec["constants"]["ascii_key"]),
                     bool(rec["constants"]["u32_key"]),
                     sorted(rec["markers"])))
    for rec in report["psb_samples"]:
        print("  PSB", os.path.relpath(rec["path"], REPO),
              "plain" if rec.get("plain_psb") else "filtered/raw", rec.get("size"))
    print("json:", args.json_out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
