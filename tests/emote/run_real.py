#!/usr/bin/env python3
"""Real-resource check of the kk4s E-mote runtime (needs a local game copy).

  python3 tests/emote/run_real.py EMOTE_TOOL --game "PATH/NekoPara Vol.1" [--json-out PATH]

For every entry of the game's emote*.xp3 archives:

* reads the stored bytes with the research XP3 reader (read-only), removes the
  archive layer (TinyFilter by adlr, checked with adler32; magic-XOR fallback)
  -- this is the archive layer, outside the E-mote core;
* pipes the unwrapped file into EMOTE_TOOL (the C++ pipeline: PSB core,
  cipher profile, adapter, runtime, scene, textures) with the SHA-256 of the
  game's E-mote DLLs as binary evidence;
* cross-checks the decrypted container against the independent Python decoder
  (research/emote-psb/emote_psb.py) by adler32;
* checks that non-PSB entries are refused as not-psb, and that a sample is
  refused when structural detection is off and no evidence is given.

Nothing is written next to the game; archives are opened read-only and their
size/mtime are compared before and after. No game data is stored in the repo.
Exit 0 only when every check passes.
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "research", "emote-psb"))
import emote_psb as E  # noqa: E402  (independent reference decoder)
import extract_xp3 as X  # noqa: E402  (XP3 reader of the NekoPara layout)

ARCHIVES = ["emote.xp3", "emotewin.xp3", "emotewin_low.xp3"]
DLLS = ["plugin/emoteplayer.dll", "emotedriver.dll"]


def tiny_filter_key(adlr):
    """Same formula as compat/cxdec/tinyfilter.cpp."""
    x = (adlr ^ 0x1548E29C) & 0xFFFFFFFF
    k = ((x >> 24) ^ (x >> 16) ^ (x >> 8) ^ x) & 0xFF
    return k or 0xD7


def xor_all(data, k):
    if not k:
        return data
    n = len(data)
    return (int.from_bytes(data, "little") ^ int.from_bytes(bytes([k]) * n, "little")).to_bytes(n, "little")


def unwrap(raw, adlr):
    """Archive layer: returns (bytes, method)."""
    if raw[:4] in (b"PSB\0", b"mdf\0"):
        return raw, "none"
    if adlr is not None:
        d = xor_all(raw, tiny_filter_key(adlr))
        if (zlib.adler32(d) & 0xFFFFFFFF) == adlr:
            return d, "tinyfilter"
    if len(raw) >= 4:
        k = raw[0] ^ 0x50
        if all(raw[i] ^ k == b"PSB\0"[i] for i in range(4)):
            return xor_all(raw, k), "magic-xor"
    return raw, "none"


def stat_sig(p):
    st = os.stat(p)
    return (st.st_size, st.st_mtime_ns)


def run_tool(tool, data, args):
    r = subprocess.run([tool] + args + ["-"], input=data, capture_output=True)
    if r.returncode not in (0, 1):
        return {"ok": False, "error": "crash", "message": "exit %d" % r.returncode}
    try:
        return json.loads(r.stdout.decode("utf-8"))
    except ValueError:
        return {"ok": False, "error": "bad-json", "message": r.stdout[:200].decode("utf-8", "replace")}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tool")
    ap.add_argument("--game", default=os.environ.get("KK4S_NEKOPARA_DIR", os.path.join(REPO, "game", "NekoPara Vol.1")))
    ap.add_argument("--json-out", default=os.path.join(REPO, "out", "emote-real", "report.json"))
    ap.add_argument("--no-python-crosscheck", action="store_true")
    a = ap.parse_args()
    a.tool = os.path.abspath(a.tool)

    if not os.path.isdir(a.game):
        print("no game directory at %s (skipped: real-resource test needs a local copy)" % a.game)
        return 2
    hashes = []
    for rel in DLLS:
        p = os.path.join(a.game, rel)
        if os.path.exists(p):
            with open(p, "rb") as f:
                hashes.append(hashlib.sha256(f.read()).hexdigest())
    ev_args = sum((["--sha256", h] for h in hashes), [])

    before = {n: stat_sig(os.path.join(a.game, n)) for n in ARCHIVES if os.path.exists(os.path.join(a.game, n))}
    t0 = time.time()
    records, failures = [], []
    agg = {"psb": 0, "loaded": 0, "non_psb": 0, "non_psb_rejected": 0, "crosscheck_ok": 0, "crosscheck_bad": 0}
    sums = {}
    first_psb = None
    for arch in ARCHIVES:
        path = os.path.join(a.game, arch)
        if not os.path.exists(path):
            continue
        with open(path, "rb") as f:
            for idx, (info, segm, real) in enumerate(X.entries(X.read_index(f))):
                name = real or info.get("name") or "#%d" % idx
                raw = X.extract(f, segm, bool(info["flags"] & 1))
                data, method = unwrap(raw, info.get("adlr"))
                is_psb = data[:4] in (b"PSB\0", b"mdf\0")
                rep = run_tool(a.tool, data, ev_args)
                rec = {"archive": arch, "entry": idx, "name": name, "filter": method, "result": rep}
                if not is_psb:
                    agg["non_psb"] += 1
                    if rep.get("error") == "not-psb":
                        agg["non_psb_rejected"] += 1
                    else:
                        failures.append("%s#%d %s: non-PSB not refused: %s" % (arch, idx, name, rep.get("error")))
                    records.append(rec)
                    continue
                agg["psb"] += 1
                if first_psb is None:
                    first_psb = data
                if not rep.get("ok"):
                    failures.append("%s#%d %s: %s %s" % (arch, idx, name, rep.get("error"), rep.get("message")))
                    records.append(rec)
                    continue
                agg["loaded"] += 1
                for k in ("variables", "timelines", "tracks", "keys", "text_keys", "objects", "motions", "layers",
                          "layer_frames", "content_frames", "mesh_bp_frames", "refs_unresolved", "icons",
                          "icons_rl", "icons_atlas", "textures", "textures_ok", "textures_unsupported", "warnings",
                          "controllers"):
                    sums[k] = sums.get(k, 0) + rep.get(k, 0)
                for grp in ("refs", "layer_frame_types", "easing_codes", "texture_formats"):
                    for k, v in rep.get(grp, {}).items():
                        sums.setdefault(grp, {})
                        sums[grp][k] = sums[grp].get(k, 0) + v
                for k in ("runtime", "scene"):
                    for kk, v in rep.get(k, {}).items():
                        sums.setdefault(k, {})
                        sums[k][kk] = sums[k].get(kk, 0) + v
                for key in ("confidence", "profile", "version_support", "adapter"):
                    sums.setdefault(key, {})
                    sums[key][rep.get(key)] = sums[key].get(rep.get(key), 0) + 1
                for k in rep.get("unknown_root_keys", []):
                    sums.setdefault("unknown_root_keys", {})
                    sums["unknown_root_keys"][k] = sums["unknown_root_keys"].get(k, 0) + 1
                checks = [
                    (rep.get("refs_unresolved") == 0, "unresolved references"),
                    # RGBA8 must decode; other formats are a controlled unsupported-feature
                    (rep.get("textures") == rep.get("textures_ok") + rep.get("textures_unsupported", 0)
                     and rep.get("textures_ok") >= rep.get("texture_formats", {}).get("RGBA8", 0),
                     "texture check: " + rep.get("texture_errors", "")),
                    (rep["runtime"]["errors"] == 0 and rep["runtime"]["nonfinite"] == 0, "runtime"),
                    (rep["scene"]["errors"] == 0 and rep["scene"]["unresolved"] == 0, "scene"),
                    (rep.get("confidence") == ("binary-evidence" if hashes else "structure-only"), "confidence"),
                ]
                if not a.no_python_crosscheck:
                    try:
                        _, pyrep = E.decrypt_psb(data, xp3_xor="none")
                        same = "%08x" % pyrep["adler32_plain"] == rep.get("adler32_plain")
                    except E.PSBError as e:
                        same = False
                    agg["crosscheck_ok" if same else "crosscheck_bad"] += 1
                    checks.append((same, "python decoder disagrees"))
                for ok, what in checks:
                    if not ok:
                        failures.append("%s#%d %s: %s" % (arch, idx, name, what))
                records.append(rec)

    # Evidence policy on a real file.
    if first_psb is not None:
        r1 = run_tool(a.tool, first_psb, [])
        r2 = run_tool(a.tool, first_psb, ["--no-structural"])
        r3 = run_tool(a.tool, first_psb, ["--profile", "emote.xorshift128.nekopara-vol1"])
        policy = {"no_evidence": r1.get("confidence"), "no_structural": r2.get("error"), "pinned": r3.get("confidence")}
        if policy != {"no_evidence": "structure-only", "no_structural": "unknown-cipher-profile", "pinned": "pinned"}:
            failures.append("evidence policy: %s" % policy)
        agg["policy"] = policy

    after = {n: stat_sig(os.path.join(a.game, n)) for n in before}
    if after != before:
        failures.append("an input archive changed during the run")
    agg["inputs_unchanged"] = after == before
    agg["dll_sha256"] = hashes
    agg["seconds"] = round(time.time() - t0, 1)

    os.makedirs(os.path.dirname(a.json_out), exist_ok=True)
    with open(a.json_out, "w", encoding="utf-8") as f:
        json.dump({"summary": agg, "sums": sums, "failures": failures, "records": records}, f, ensure_ascii=False,
                  indent=1)
    print(json.dumps(agg, ensure_ascii=False))
    print(json.dumps(sums, ensure_ascii=False))
    for fl in failures[:40]:
        print("FAIL", fl)
    print("emote real: %d PSB, %d loaded, %d non-PSB (%d refused), %d failures"
          % (agg["psb"], agg["loaded"], agg["non_psb"], agg["non_psb_rejected"], len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
