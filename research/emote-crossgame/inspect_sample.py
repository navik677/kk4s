#!/usr/bin/env python3
"""Read-only structural validation of a PSB sample with the known decoder.

Works on a plain/filtered PSB file, or on one entry of an XP3 archive (the
archive is opened read-only and the entry is extracted in memory).  It reports
the archive filter candidate, Adler-32, header layout, decrypted structure,
round-trip and whether bytes outside the encrypted region changed.

Nothing is written.  Exit status is 0 only when every check passes.

Usage:
  python3 research/emote-crossgame/inspect_sample.py FILE.psb [--key N]
  python3 research/emote-crossgame/inspect_sample.py --xp3 A.xp3 --entry NAME
      [--key N] [--adler HEX] [--json]
"""
import argparse
import hashlib
import json
import os
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
PSB_DIR = os.path.abspath(os.path.join(HERE, "..", "emote-psb"))
sys.path.insert(0, PSB_DIR)
import emote_psb as E  # noqa: E402
import extract_xp3 as X  # noqa: E402


def tiny_filter_key(adlr):
    """Known XP3 TinyFilter formula (separate from the PSB cipher)."""
    x = (adlr ^ 0x1548E29C) & 0xFFFFFFFF
    k = ((x >> 24) ^ (x >> 16) ^ (x >> 8) ^ x) & 0xFF
    return k or 0xD7


def read_xp3_entry(path, wanted):
    with open(path, "rb") as f:
        entries = list(X.entries(X.read_index(f)))
        if wanted.isdigit():
            idx = int(wanted)
            if not 0 <= idx < len(entries):
                raise E.PSBError("entry index %d outside %d entries" % (idx, len(entries)))
            info, segm, real = entries[idx]
        else:
            matches = [(i, e) for i, e in enumerate(entries)
                       if wanted in ((e[2] or e[0].get("name") or ""))]
            if not matches:
                raise E.PSBError("no entry matching %r" % wanted)
            if len(matches) > 1:
                raise E.PSBError("%d entries match %r; use an index" % (len(matches), wanted))
            idx, (info, segm, real) = matches[0]
        raw = X.extract(f, segm, bool(info["flags"] & 1))
    return raw, info.get("adlr"), real or info.get("name")


def pick_unfiltered(raw, adlr, mode, adler_expected):
    """Return (data, key, method, adler_stored, adler_unfiltered)."""
    adler_stored = zlib.adler32(raw) & 0xFFFFFFFF
    candidates = [(raw, 0, "none")]
    if mode != "none" and raw[:4] != E.MAGIC and len(raw) >= 4:
        k = raw[0] ^ E.MAGIC[0]
        if all(raw[i] ^ k == E.MAGIC[i] for i in range(4)):
            candidates.append((bytes(b ^ k for b in raw), k, "magic-xor"))
    if mode != "none" and adlr is not None and raw[:4] != E.MAGIC:
        k = tiny_filter_key(adlr)
        if k:
            candidates.append((bytes(b ^ k for b in raw), k, "tinyfilter"))
    for data, key, method in candidates:
        adler = zlib.adler32(data) & 0xFFFFFFFF
        if adler_expected is not None and adler != adler_expected:
            continue
        if data[:4] == E.MAGIC:
            return data, key, method, adler_stored, adler
    if adler_expected is not None:
        raise E.PSBError("no filter candidate matches adler32 %08x" % adler_expected)
    raise E.PSBError("no filter candidate yields PSB magic")


def inspect_bytes(raw, key=E.DEFAULT_KEY, adlr=None, adler=None, xp3_xor="auto"):
    data, fkey, method, adler_stored, adler_unfiltered = pick_unfiltered(
        raw, adlr, xp3_xor, adler)
    plain, rep = E.decrypt_psb(data, key=key, xp3_xor="none")
    start = rep["encrypted_region"]["start"]
    end = rep["encrypted_region"]["end"]
    again = E.crypt_region(plain, start, end, key)
    checks = {
        "adler_ok": adler is None or adler_unfiltered == adler,
        "structure_ok": True,  # decrypt_psb validated trie/strings/chunks/root
        "roundtrip": again == data,
        "outside_unchanged": (plain[:start] == data[:start]
                              and plain[end:] == data[end:]),
        "region_changed": plain[start:end] != data[start:end],
        "trie_tight": bool(rep.get("trie_tight")),
    }
    checks["ok"] = all(v for k, v in checks.items() if k != "ok")
    return {
        "size": len(raw),
        "sha256_stored": hashlib.sha256(raw).hexdigest(),
        "sha256_plain": rep["sha256_plain"],
        "adlr": adlr,
        "adler_stored": adler_stored,
        "adler_unfiltered": adler_unfiltered,
        "filter_key": fkey,
        "filter_method": method,
        "version": rep["version"],
        "encrypt": 0,
        "header_len": rep["header_len"],
        "offsets": rep["offsets"],
        "encrypted_region": rep["encrypted_region"],
        "names": rep["names"],
        "strings": rep["strings"],
        "resources": rep["resources"],
        "trie_end": rep["trie_end"],
        "root_type": rep["root_type"],
        "root_keys": rep["root_keys"],
        "checks": checks,
    }


def main(argv=None):
    ap = argparse.ArgumentParser()
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("input", nargs="?")
    src.add_argument("--xp3")
    ap.add_argument("--entry", default=None,
                    help="entry index or unique name substring (with --xp3)")
    ap.add_argument("--key", default=str(E.DEFAULT_KEY))
    ap.add_argument("--adler", default=None, help="expected adlr (hex) for an XP3 entry")
    ap.add_argument("--xp3-xor", default="auto", choices=("auto", "none"))
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args(argv)

    try:
        if args.xp3:
            if args.entry is None:
                raise E.PSBError("--xp3 requires --entry")
            raw, adlr, name = read_xp3_entry(args.xp3, args.entry)
            source = "%s#%s" % (args.xp3, name)
        else:
            with open(args.input, "rb") as f:
                raw = f.read()
            adlr = None
            source = args.input
        expected = None
        if args.adler is not None:
            expected = int(args.adler, 16)
        elif adlr is not None:
            expected = adlr
        key = E._uint(args.key)
        rec = inspect_bytes(raw, key=key, adlr=adlr, adler=expected,
                            xp3_xor=args.xp3_xor)
        rec["source"] = source
        rec["key"] = key
        if args.json:
            print(json.dumps(rec, ensure_ascii=False, indent=2))
        else:
            print("%s: PSB v%d, key %d, filter %s(%#x), region %#x..%#x"
                  % (source, rec["version"], key, rec["filter_method"],
                     rec["filter_key"], rec["encrypted_region"]["start"],
                     rec["encrypted_region"]["end"]))
            print("  names %d, strings %d, resources %d, trie_tight=%s"
                  % (rec["names"], rec["strings"], rec["resources"],
                     rec["checks"]["trie_tight"]))
            print("  checks: %s" % ", ".join(
                "%s=%s" % (k, v) for k, v in rec["checks"].items()))
        return 0 if rec["checks"]["ok"] else 1
    except (E.PSBError, OSError) as e:
        if args.json:
            print(json.dumps({"source": args.xp3 or args.input,
                              "checks": {"ok": False}, "error": str(e)},
                             ensure_ascii=False, indent=2))
        else:
            print("error: %s" % e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
