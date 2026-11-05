#!/usr/bin/env python3
"""Tests for the cross-game tools.  No game files are required.

Run: python3 research/emote-crossgame/test_crossgame_tools.py -v
"""
import json
import os
import sys
import tempfile
import unittest
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "emote-psb"))

import audit_dll  # noqa: E402
import inspect_sample  # noqa: E402
import scan_environment  # noqa: E402
import test_emote_psb as T  # noqa: E402
import emote_psb as E  # noqa: E402


class ConstantScanTests(unittest.TestCase):
    def test_find_offsets(self):
        data = b"xx742877301yy742877301"
        self.assertEqual(audit_dll.find_offsets(data, b"742877301"), [2, 13])

    def test_key_push_reference_pattern(self):
        # A minimal synthetic .text-like buffer is enough for the pure search.
        va = 0x100D4394
        buf = b"\x90" + b"\x68" + va.to_bytes(4, "little") + b"\x90"
        self.assertEqual(audit_dll.find_offsets(buf, b"\x68" + va.to_bytes(4, "little")),
                         [1])

    def test_seed_constants_are_little_endian(self):
        self.assertEqual(audit_dll.SEEDS["seed1"].to_bytes(4, "little"), b"\x15\xcd\x5b\x07")
        self.assertEqual(audit_dll.KEY_U32, b"\x75\xc2\x46\x2c")


class InspectTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.plain, self.stored, self.header = T.make()
        self.path = os.path.join(self.tmp.name, "sample.psb")
        with open(self.path, "wb") as f:
            f.write(self.stored)

    def test_plain_sample_passes(self):
        rec = inspect_sample.inspect_bytes(self.stored)
        self.assertTrue(rec["checks"]["ok"], rec)
        self.assertTrue(rec["checks"]["roundtrip"])
        self.assertTrue(rec["checks"]["outside_unchanged"])
        self.assertEqual(rec["encrypted_region"]["start"], self.header.header_len)

    def test_wrong_key_fails_without_output(self):
        before = sorted(os.listdir(self.tmp.name))
        rec = inspect_sample.main([self.path, "--key", str(E.DEFAULT_KEY ^ 1),
                                  "--json"])
        # main prints JSON; call the pure function to assert the failure shape
        with self.assertRaises(E.PSBError):
            inspect_sample.inspect_bytes(self.stored, key=E.DEFAULT_KEY ^ 1)
        self.assertEqual(before, sorted(os.listdir(self.tmp.name)))
        self.assertEqual(rec, 1)

    def test_filtered_sample_with_adler(self):
        fkey = 0x5A
        filtered = bytes(b ^ fkey for b in self.stored)
        adlr = zlib.adler32(self.stored) & 0xFFFFFFFF
        rec = inspect_sample.inspect_bytes(filtered, adlr=adlr, adler=adlr)
        self.assertTrue(rec["checks"]["ok"], rec)
        self.assertEqual(rec["filter_key"], fkey)
        self.assertEqual(rec["adler_unfiltered"], adlr)

    def test_cli_writes_nothing(self):
        before = sorted(os.listdir(self.tmp.name))
        rc = inspect_sample.main([self.path, "--json"])
        self.assertEqual(rc, 0)
        self.assertEqual(before, sorted(os.listdir(self.tmp.name)))


class EnvironmentScanTests(unittest.TestCase):
    def test_scan_finds_psb_and_writes_only_json(self):
        with tempfile.TemporaryDirectory() as tmp:
            sample = os.path.join(tmp, "x.psb")
            _, stored, _ = T.make()
            with open(sample, "wb") as f:
                f.write(stored)
            out = os.path.join(tmp, "out", "scan.json")
            rc = scan_environment.main(["--root", tmp, "--json-out", out])
            self.assertEqual(rc, 0)
            with open(out) as f:
                rec = json.load(f)
            self.assertEqual(len(rec["psb_samples"]), 1)
            self.assertTrue(rec["psb_samples"][0]["plain_psb"])
            self.assertEqual(sorted(os.listdir(tmp)), ["out", "x.psb"])


if __name__ == "__main__":
    unittest.main()
