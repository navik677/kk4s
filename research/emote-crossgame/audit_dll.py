#!/usr/bin/env python3
"""Read-only audit of a PE binary for E-mote PSB-cipher evidence.

Reports, per file:

* SHA-256, size, machine, image base, build timestamp;
* VS_VERSIONINFO strings (FileVersion/ProductName/...), when present;
* the ASCII decimal key ``742877301`` and where it lives;
* the key as a little-endian u32 immediate, if present at all;
* the three xorshift seeds as little-endian u32 values and their sections;
* code references to the key *string* (``push imm32`` pointing at it).

A key string that is referenced by code and written into the same state block
as the three seeds is evidence of use.  A bare constant with no reference is
only a coincidence.  This tool never executes or patches the binary.

Usage:
  python3 research/emote-crossgame/audit_dll.py FILE.dll [FILE.exe ...] \
      [--json-out research/emote-crossgame/work/audit_dll.json]
"""
import argparse
import datetime
import hashlib
import json
import os
import struct
import sys

try:
    import pefile
except ImportError:  # pragma: no cover - reported at runtime
    pefile = None

KEY_TEXT = b"742877301"
KEY_U32 = struct.pack("<I", 0x2C46C275)
SEEDS = {
    "seed1": 123456789,
    "seed2": 362436069,
    "seed3": 521288629,
}
MARKERS = (b"E-mote", b"emoteplayer", b"emotedriver", b"psbfile",
           b"M2 CheeseWare", b"PSB\0")


def find_offsets(data, needle):
    out, pos = [], 0
    while True:
        pos = data.find(needle, pos)
        if pos < 0:
            return out
        out.append(pos)
        pos += 1


def section_at(pe, file_off):
    for s in pe.sections:
        start = s.PointerToRawData
        if start <= file_off < start + s.SizeOfRawData:
            name = s.Name.rstrip(b"\0").decode("latin1")
            va = pe.OPTIONAL_HEADER.ImageBase + s.VirtualAddress + (file_off - start)
            return name, va
    return None, None


def version_strings(pe):
    out = {}
    try:
        pe.parse_data_directories(
            directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_RESOURCE"]])
    except Exception:
        return out
    for group in getattr(pe, "FileInfo", []) or []:
        for entry in group:
            for table in getattr(entry, "StringTable", []) or []:
                for k, v in table.entries.items():
                    out[k.decode("utf-8", "replace")] = v.decode("utf-8", "replace")
    return out


def key_references(pe, key_va):
    """Find ``push imm32`` instructions whose immediate is ``key_va``."""
    pat = b"\x68" + struct.pack("<I", key_va)
    refs = []
    for s in pe.sections:
        if s.Name.rstrip(b"\0") != b".text":
            continue
        raw = s.get_data()
        base = s.PointerToRawData
        for pos in find_offsets(raw, pat):
            off = base + pos
            name, va = section_at(pe, off)
            refs.append({
                "file_offset": off,
                "va": va,
                "opcode": "push imm32",
                "target_va": key_va,
                "section": name,
            })
    return refs


def audit_pe(path):
    if pefile is None:
        raise RuntimeError("pefile is required for DLL auditing")
    data = open(path, "rb").read()
    pe = pefile.PE(data=data, fast_load=True)
    rec = {
        "path": path,
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "machine": "0x%x" % pe.FILE_HEADER.Machine,
        "image_base": "0x%x" % pe.OPTIONAL_HEADER.ImageBase,
        "timestamp": datetime.datetime.utcfromtimestamp(
            pe.FILE_HEADER.TimeDateStamp).isoformat() + "Z",
        "sections": [
            {
                "name": s.Name.rstrip(b"\0").decode("latin1"),
                "va": "0x%x" % s.VirtualAddress,
                "vsize": s.Misc_VirtualSize,
                "raw_offset": s.PointerToRawData,
                "raw_size": s.SizeOfRawData,
            }
            for s in pe.sections
        ],
        "version": version_strings(pe),
        "constants": {},
        "key_string": None,
        "key_references": [],
        "markers": {},
    }

    ascii_offs = find_offsets(data, KEY_TEXT)
    u32_offs = find_offsets(data, KEY_U32)
    rec["constants"]["ascii_key"] = ascii_offs
    rec["constants"]["u32_key"] = u32_offs
    if ascii_offs:
        name, va = section_at(pe, ascii_offs[0])
        rec["key_string"] = {
            "file_offset": ascii_offs[0],
            "va": "0x%x" % va if va is not None else None,
            "section": name,
        }
        if va is not None:
            rec["key_references"] = key_references(pe, va)

    seeds = {}
    for label, value in SEEDS.items():
        offs = find_offsets(data, struct.pack("<I", value))
        seeds[label] = [
            {"file_offset": off, "section": section_at(pe, off)[0],
             "va": "0x%x" % section_at(pe, off)[1] if section_at(pe, off)[1] is not None else None}
            for off in offs
        ]
    rec["constants"]["seeds"] = seeds

    for marker in MARKERS:
        offs = find_offsets(data, marker)
        if offs:
            rec["markers"][marker.decode("latin1")] = len(offs)

    text = b"".join(s.get_data() for s in pe.sections
                    if s.Name.rstrip(b"\0") == b".text")
    rec["assess"] = {
        "key_string_present": bool(ascii_offs),
        "key_u32_immediate_present": bool(u32_offs),
        "key_referenced_from_code": bool(rec["key_references"]),
        "all_three_seeds_present": all(seeds[k] for k in SEEDS),
        "emote_markers": sorted(rec["markers"]),
        "note": ("constant + code reference + seeds = candidate cipher init; "
                 "this still needs a real sample to prove behaviour"),
    }
    return rec


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--json-out", default=None)
    args = ap.parse_args(argv)

    out = []
    for path in args.files:
        try:
            rec = audit_pe(path)
        except Exception as e:
            rec = {"path": path, "error": "%s: %s" % (type(e).__name__, e)}
        out.append(rec)
        if "error" in rec:
            print("ERROR %s: %s" % (path, rec["error"]))
            continue
        ver = rec["version"].get("FileVersion", "-")
        print("%-58s %s  ver=%-10s key_text=%s u32=%s refs=%d seeds=%s"
              % (os.path.basename(path), rec["sha256"][:12], ver,
                 bool(rec["constants"]["ascii_key"]),
                 bool(rec["constants"]["u32_key"]),
                 len(rec["key_references"]),
                 all(rec["constants"]["seeds"][k] for k in SEEDS)))
        print("   key_string=%s refs=%s markers=%s"
              % (rec["key_string"], rec["key_references"],
                 sorted(rec["markers"])))

    if args.json_out:
        parent = os.path.dirname(os.path.abspath(args.json_out))
        os.makedirs(parent, exist_ok=True)
        with open(args.json_out, "w") as f:
            json.dump(out, f, indent=1)
        print("json:", args.json_out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
