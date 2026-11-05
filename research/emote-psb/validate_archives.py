#!/usr/bin/env python3
"""Validate the E-mote PSB decoder against every entry in the local archives.

Reads the NekoPara Vol.1 XP3 archives (read-only), and for each File entry:

* extracts the stored bytes (segments, zlib if flagged);
* determines the archive TinyFilter byte from adlr and checks
  ``adler32(unfiltered) == adlr``;
* checks the PSB magic, header invariants and encrypted-region bounds;
* decrypts the region with the recovered cipher and validates the name trie,
  strings, chunk arrays and root object;
* re-encrypts and requires byte-for-byte equality with the stored file;
* cross-checks the fast XOR path against ``emote_psb.crypt_region``.

Nothing is written outside ``research/emote-psb/work/`` and inputs are opened
read-only.

Usage:
  python3 research/emote-psb/validate_archives.py [--json-out PATH]
"""
import argparse
import json
import os
import sys
import time
import zlib
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import emote_psb as E  # noqa: E402
import extract_xp3 as X  # noqa: E402

GAME = os.path.abspath(os.path.join(HERE, "..", "..", "game", "NekoPara Vol.1"))
ARCHIVES = ["emote.xp3", "emotewin.xp3", "emotewin_low.xp3"]
M32 = 0xFFFFFFFF


def tiny_filter_key(adlr):
    """Same formula as compat/cxdec/tinyfilter.cpp."""
    x = (adlr ^ 0x1548E29C) & M32
    k = ((x >> 24) ^ (x >> 16) ^ (x >> 8) ^ x) & 0xFF
    return k or 0xD7


class Keystream:
    """Precomputed xorshift128 keystream (one shared stream for all files)."""

    def __init__(self):
        self.buf = b""
        self.mv = None

    def ensure(self, n):
        if len(self.buf) >= n:
            return
        # Byte-exact: a word may yield fewer than four bytes when its high
        # bytes are zero, because the reader draws a new word whenever the
        # shifted value becomes 0 (this happens for ~1/256 of words).
        buf = bytearray(self.buf)
        append = buf.append
        x, y, z = E.DEFAULT_SEEDS
        w = E.DEFAULT_KEY
        cur = 0
        while len(buf) < n:
            if cur == 0:
                t = (x ^ ((x << 11) & M32)) & M32
                x, y, z = y, z, w
                w = (w ^ (w >> 19) ^ t ^ (t >> 8)) & M32
                cur = w
            append(cur & 0xFF)
            cur >>= 8
        self.buf = bytes(buf)
        self.mv = memoryview(self.buf)


def xor_region(data, start, end, ks):
    """XOR [start,end) with the shared keystream; returns new bytes."""
    n = end - start
    ks.ensure(n)
    out = bytearray(data)
    mv = memoryview(data)
    km = ks.mv
    for off in range(0, n, 1 << 16):
        m = min(1 << 16, n - off)
        a = int.from_bytes(mv[start + off:start + off + m], "little")
        b = int.from_bytes(km[off:off + m], "little")
        out[start + off:start + off + m] = (a ^ b).to_bytes(m, "little")
    return bytes(out)


def xor_byte(data, k):
    if not k:
        return data
    out = bytearray(data)
    mv = memoryview(data)
    mask = bytes([k]) * (1 << 16)
    mb = memoryview(mask)
    for off in range(0, len(data), 1 << 16):
        m = min(1 << 16, len(data) - off)
        a = int.from_bytes(mv[off:off + m], "little")
        b = int.from_bytes(mb[:m], "little")
        out[off:off + m] = (a ^ b).to_bytes(m, "little")
    return bytes(out)


def stat_sig(path):
    st = os.stat(path)
    return {"size": st.st_size, "mtime_ns": st.st_mtime_ns}


def validate_file(raw, adlr, ks, do_ref=True):
    """Return a per-entry record. Raises nothing; errors are recorded."""
    rec = {
        "size": len(raw),
        "adlr": adlr,
        "adler_stored": zlib.adler32(raw) & M32,
        "filter": 0,
        "filter_formula": None,
        "adler_match": None,
        "magic": raw[:4].hex(),
        "psb": False,
        "error": None,
    }
    kdet = raw[0] ^ E.MAGIC[0] if len(raw) >= 4 else 0
    det_ok = len(raw) >= 4 and all(raw[i] ^ kdet == E.MAGIC[i] for i in range(4))
    kform = tiny_filter_key(adlr) if adlr is not None else 0

    data = raw
    if adlr is not None and rec["adler_stored"] == adlr:
        rec["adler_match"] = True
    else:
        for k in (kdet if det_ok else None, kform):
            if not k:
                continue
            cand = xor_byte(raw, k)
            if (zlib.adler32(cand) & M32) == adlr:
                data = cand
                rec["filter"] = k
                rec["filter_formula"] = (k == kform)
                rec["adler_match"] = True
                break
        else:
            if adlr is not None:
                rec["adler_match"] = False
            # fall back to magic detection so the PSB can still be validated
            if det_ok:
                data = xor_byte(raw, kdet)
                rec["filter"] = kdet
                rec["filter_formula"] = (kdet == kform)

    rec["magic"] = data[:4].hex()
    if data[:4] != E.MAGIC:
        rec["kind"] = "non-psb"
        return rec

    rec["psb"] = True
    rec["kind"] = "psb"
    try:
        h = E.parse_header(data)
        E.check_header(h, len(data))
        start, end = h.header_len, h.off_choffs
        rec.update({
            "version": h.version,
            "encrypt": h.encrypt,
            "header_len": start,
            "off_names": h.off_names,
            "off_choffs": end,
            "region": end - start,
            "header_len_eq_names": start == h.off_names,
            "end_is_array": bool(0x0D <= data[end] <= 0x10),
            "start_encrypted_looking": not (0x0D <= data[h.off_names] <= 0x10),
        })

        plain = xor_region(data, start, end, ks)

        rec["outside_unchanged"] = (plain[:start] == data[:start]
                                    and plain[end:] == data[end:])
        if do_ref:
            n = min(1 << 16, end - start)
            ref = E.crypt_region(data[:start + n], start, start + n)
            rec["ref_prefix_ok"] = ref[start:] == plain[start:start + n]

        names, trie_end = E.decode_names(plain, h)
        strings = E.decode_strings(plain, h)
        coffs, clens = E.decode_chunks(data, h)
        root = E.read_value(plain, h.off_entries, names, strings, len(coffs))

        rec.update({
            "names": len(names),
            "strings": len(strings),
            "resources": len(coffs),
            "trie_end": trie_end,
            "trie_tight": trie_end == h.off_entries,
            "root_type": type(root).__name__,
            "root_keys": list(root) if isinstance(root, dict) else None,
        })
        if not isinstance(root, dict) or not root:
            raise E.PSBError("root is not a non-empty object")

        again = xor_region(plain, start, end, ks)
        rec["roundtrip"] = again == data
        rec["plain_sha256"] = __import__("hashlib").sha256(plain).hexdigest()
        if not rec["roundtrip"]:
            raise E.PSBError("re-encryption differs from the stored file")
        if plain[start:end] == data[start:end]:
            raise E.PSBError("encrypted region is unchanged (keystream not applied)")
    except Exception as e:  # report, do not abort the archive
        rec["error"] = "%s: %s" % (type(e).__name__, e)
    return rec


def name_short(name, n=72):
    name = name or "?"
    return name if len(name) <= n else name[:n] + "..."


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--json-out", default=os.path.join(HERE, "work", "validation.json"))
    ap.add_argument("--archives", nargs="*", default=ARCHIVES)
    args = ap.parse_args(argv)

    before = {a: stat_sig(os.path.join(GAME, a)) for a in args.archives}
    ks = Keystream()
    report = {"archives": {}, "files": [], "started": time.strftime("%Y-%m-%dT%H:%M:%S")}
    agg = Counter()

    # Index pass: sizes are needed to size the shared keystream once.
    plans = {}
    for aname in args.archives:
        with open(os.path.join(GAME, aname), "rb") as f:
            plans[aname] = list(X.entries(X.read_index(f)))
    biggest = max(info["osize"] for es in plans.values() for info, _s, _n in es)
    ks.ensure(biggest)

    for aname in args.archives:
        path = os.path.join(GAME, aname)
        t0 = time.time()
        a = {"entries": 0, "psb": 0, "decrypted": 0, "validated": 0,
             "roundtrip": 0, "adler_ok": 0, "adler_bad": [], "filtered": 0,
             "formula_match": 0, "formula_mismatch": [], "errors": [],
             "non_psb": [], "ref_prefix_bad": [], "boundary": Counter(),
             "header_len": Counter(), "version": Counter(), "trie_tight": 0}
        with open(path, "rb") as f:
            entries = plans[aname]
            for i, (info, segm, real) in enumerate(entries):
                shown = real or info["name"]
                a["entries"] += 1
                try:
                    raw = X.extract(f, segm, bool(info["flags"] & 1))
                except Exception as e:
                    a["errors"].append({"i": i, "name": name_short(shown),
                                        "error": "extract: %s" % e})
                    agg["errors"] += 1
                    continue
                rec = validate_file(raw, info.get("adlr"), ks)
                rec.update({"archive": aname, "index": i, "name": shown})
                report["files"].append(rec)
                agg["entries"] += 1

                if not rec["psb"]:
                    a["non_psb"].append(name_short(shown, 48))
                    if rec["adler_match"] is False:
                        a["adler_bad"].append(name_short(shown, 48))
                    continue
                a["psb"] += 1
                agg["psb"] += 1
                a["header_len"][rec.get("header_len")] += 1
                a["version"][rec.get("version")] += 1
                if rec.get("filter"):
                    a["filtered"] += 1
                    agg["filtered"] += 1
                    if rec.get("filter_formula"):
                        a["formula_match"] += 1
                    else:
                        a["formula_mismatch"].append(
                            {"name": name_short(shown), "filter": rec["filter"],
                             "formula": tiny_filter_key(info.get("adlr"))})
                if rec.get("adler_match"):
                    a["adler_ok"] += 1
                    agg["adler_ok"] += 1
                elif rec.get("adler_match") is False:
                    a["adler_bad"].append(name_short(shown))
                    agg["adler_bad"] += 1
                if rec.get("error") is None:
                    a["decrypted"] += 1
                    a["validated"] += 1
                    agg["decrypted"] += 1
                    if rec.get("roundtrip"):
                        a["roundtrip"] += 1
                        agg["roundtrip"] += 1
                    if not rec.get("ref_prefix_ok", True):
                        a["ref_prefix_bad"].append(name_short(shown))
                        agg["ref_prefix_bad"] += 1
                    if rec.get("trie_tight"):
                        a["trie_tight"] += 1
                    a["boundary"]["end_is_array"] += bool(rec.get("end_is_array"))
                    a["boundary"]["start_encrypted_looking"] += bool(rec.get("start_encrypted_looking"))
                    a["boundary"]["header_len_eq_names"] += bool(rec.get("header_len_eq_names"))
                    a["boundary"]["outside_unchanged"] += bool(rec.get("outside_unchanged"))
                else:
                    a["errors"].append({"i": i, "name": name_short(shown),
                                        "error": rec["error"]})
                    agg["errors"] += 1

        a["seconds"] = round(time.time() - t0, 2)
        report["archives"][aname] = a
        print("\n== %s ==" % aname)
        print("  entries %d | PSB %d | decrypted %d | round-trip %d | errors %d"
              % (a["entries"], a["psb"], a["decrypted"], a["roundtrip"], len(a["errors"])))
        print("  adler ok %d | bad %d | filtered %d | formula match %d | mismatch %d"
              % (a["adler_ok"], len(a["adler_bad"]), a["filtered"],
                 a["formula_match"], len(a["formula_mismatch"])))
        print("  header_len %s | versions %s | trie_tight %d/%d"
              % (dict(a["header_len"]), dict(a["version"]), a["trie_tight"], a["psb"]))
        print("  boundary: end-array %d/%d, start-looks-encrypted %d/%d, hdr==names %d/%d"
              % (a["boundary"]["end_is_array"], a["psb"],
                 a["boundary"]["start_encrypted_looking"], a["psb"],
                 a["boundary"]["header_len_eq_names"], a["psb"]))
        if a["non_psb"]:
            print("  non-PSB: %d %s" % (len(a["non_psb"]), a["non_psb"][:4]))
        if a["adler_bad"]:
            print("  ADLER BAD: %s" % a["adler_bad"][:8])
        if a["formula_mismatch"]:
            print("  FILTER FORMULA MISMATCH: %s" % a["formula_mismatch"][:8])
        if a["errors"]:
            print("  ERRORS:")
            for e in a["errors"][:8]:
                print("    #%d %s: %s" % (e["i"], e["name"], e["error"]))

    after = {a: stat_sig(os.path.join(GAME, a)) for a in args.archives}
    report["inputs_unchanged"] = before == after
    report["summary"] = dict(agg)
    report["finished"] = time.strftime("%Y-%m-%dT%H:%M:%S")

    # split adler results for dx_ and dxlow_ subsets
    def subset(prefix):
        rows = [f for f in report["files"]
                if os.path.basename(f.get("name") or "").startswith(prefix)]
        return {
            "entries": len(rows),
            "psb": sum(1 for r in rows if r.get("psb")),
            "decrypted": sum(1 for r in rows if r.get("psb") and not r.get("error")),
            "roundtrip": sum(1 for r in rows if r.get("roundtrip")),
            "adler_ok": sum(1 for r in rows if r.get("adler_match") is True),
            "adler_bad": sum(1 for r in rows if r.get("adler_match") is False),
            "filtered": sum(1 for r in rows if r.get("filter")),
            "formula_match": sum(1 for r in rows if r.get("filter_formula") is True),
        }

    report["subsets"] = {"dx": subset("dx_"), "dxlow": subset("dxlow_")}

    os.makedirs(os.path.dirname(os.path.abspath(args.json_out)), exist_ok=True)
    with open(args.json_out, "w") as f:
        json.dump(report, f, ensure_ascii=False, indent=1)

    print("\n== total ==")
    print("  entries %d | PSB %d | decrypted %d | round-trip %d | adler ok %d | adler bad %d"
          % (agg["entries"], agg["psb"], agg["decrypted"], agg["roundtrip"],
             agg["adler_ok"], agg["adler_bad"]))
    outside_bad = sum(1 for f in report["files"]
                      if f.get("psb") and not f.get("outside_unchanged"))
    print("  reference-prefix mismatches %d | outside-region changes %d"
          % (agg["ref_prefix_bad"], outside_bad))
    for k, v in report["subsets"].items():
        print("  %s: %s" % (k, v))
    print("  inputs unchanged: %s" % report["inputs_unchanged"])
    print("  json: %s" % args.json_out)

    ok = (agg["errors"] == 0 and agg["adler_bad"] == 0 and agg["ref_prefix_bad"] == 0
          and outside_bad == 0
          and agg["decrypted"] == agg["psb"] == agg["roundtrip"]
          and report["inputs_unchanged"])
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
