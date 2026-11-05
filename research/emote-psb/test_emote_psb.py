#!/usr/bin/env python3
"""Tests for emote_psb.py.

Everything here is synthetic (built in memory) except the last test, which uses
files under research/emote-psb/work/ if the local session extracted any; that
directory is gitignored and never part of the repository.

    python3 research/emote-psb/test_emote_psb.py -v
"""

import contextlib
import glob
import io
import json
import os
import struct
import sys
import tempfile
import unittest
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import emote_psb as E  # noqa: E402


# ------------------------------------------------------------------ writer
def uint_bytes(v, n=None):
    n = n or max(1, (v.bit_length() + 7) // 8)
    return v.to_bytes(n, "little"), n


def enc_array(vals):
    cnt, cw = uint_bytes(len(vals))
    ew = max([uint_bytes(v)[1] for v in vals] or [1])
    out = bytes([0x0C + cw]) + cnt + bytes([0x0C + ew])
    for v in vals:
        out += v.to_bytes(ew, "little")
    return out


class Writer:
    """Minimal PSB writer in the layout observed in NekoPara E-mote files:
    header, name trie, root entry, string offsets, string data, chunk arrays."""

    def __init__(self):
        self.strings, self.names, self.resources = [], [], []

    def sidx(self, s):
        if s not in self.strings:
            self.strings.append(s)
        return self.strings.index(s)

    def nidx(self, s):
        if s not in self.names:
            self.names.append(s)
        return self.names.index(s)

    def value(self, v):
        if isinstance(v, int) and not isinstance(v, bool):
            if v == 0:
                return b"\x04"
            n = 1
            while not -(1 << (8 * n - 1)) <= v < (1 << (8 * n - 1)):
                n += 1
            return bytes([4 + n]) + v.to_bytes(n, "little", signed=True)
        if v is None:
            return b"\x01"
        if v is True:
            return b"\x03"
        if v is False:
            return b"\x02"
        if isinstance(v, float):
            if v == 0.0:
                return b"\x1d"
            if struct.unpack("<f", struct.pack("<f", v))[0] == v:
                return b"\x1e" + struct.pack("<f", v)
            return b"\x1f" + struct.pack("<d", v)
        if isinstance(v, str):
            b, n = uint_bytes(self.sidx(v))
            return bytes([0x14 + n]) + b
        if isinstance(v, tuple) and v and v[0] == "res":
            self.resources.append(v[1])
            b, n = uint_bytes(len(self.resources) - 1)
            return bytes([0x18 + n]) + b
        if isinstance(v, list):
            items = [self.value(x) for x in v]
            offs, pos = [], 0
            for it in items:
                offs.append(pos)
                pos += len(it)
            return b"\x20" + enc_array(offs) + b"".join(items)
        if isinstance(v, dict):
            items = [self.value(x) for x in v.values()]
            offs, pos = [], 0
            for it in items:
                offs.append(pos)
                pos += len(it)
            return b"\x21" + enc_array([self.nidx(k) for k in v]) + enc_array(offs) + b"".join(items)
        raise TypeError(v)

    def name_tables(self):
        charset, tree, leaves = {0: 256}, {}, []
        nxt = [512]
        child = {}

        def get(parent, ch):
            if (parent, ch) not in child:
                if parent not in charset:
                    charset[parent] = nxt[0]
                    nxt[0] += 256
                cid = charset[parent] + ch
                assert cid != 0 and cid not in tree
                tree[cid] = parent
                child[(parent, ch)] = cid
            return child[(parent, ch)]

        for name in self.names:
            node = 0
            for ch in name.encode("utf-8"):
                node = get(node, ch)
            leaves.append(get(node, 0))
        size = max([max(tree) if tree else 0, max(charset)]) + 1
        cs = [charset.get(i, 0) for i in range(size)]
        tr = [tree.get(i, 0) for i in range(size)]
        return enc_array(cs) + enc_array(tr) + enc_array(leaves)

    def build(self, root, version=2):
        entries = self.value(root)
        names = self.name_tables()
        offs, sdata = [], b""
        for s in self.strings:
            offs.append(len(sdata))
            sdata += s.encode("utf-8") + b"\0"
        strarr = enc_array(offs)

        coffs, clens, cdata = [], [], b""
        for r in self.resources:
            coffs.append(len(cdata))
            clens.append(len(r))
            cdata += r
        ca, cl = enc_array(coffs), enc_array(clens)

        hdr = 0x28
        o_names = hdr
        o_entries = o_names + len(names)
        o_str = o_entries + len(entries)
        o_sdata = o_str + len(strarr)
        o_choffs = o_sdata + len(sdata)
        o_chlens = o_choffs + len(ca)
        o_chdata = o_chlens + len(cl)
        head = E.MAGIC + struct.pack("<HH", version, 0) + struct.pack(
            "<8I", hdr, o_names, o_str, o_sdata, o_choffs, o_chlens, o_chdata, o_entries)
        return head + names + entries + strarr + sdata + ca + cl + cdata


ROOT = {
    "id": "テスト",
    "label": "azuki",
    "object": {"layer": [{"name": "a", "x": 1, "visible": True},
                         {"name": "b", "x": -2, "visible": False}]},
    "screenSize": [1280, 720],
    "scale": 1.5,
    "nothing": None,
    "res": ("res", b"\x00\x01\x02payload"),
    "empty": [],
}


def make(root=ROOT, version=2, key=E.DEFAULT_KEY, xp3_byte=None):
    plain = Writer().build(root, version=version)
    h = E.parse_header(plain)
    stored = E.crypt_region(plain, h.header_len, h.off_choffs, key)
    if xp3_byte is not None:
        stored = bytes(b ^ xp3_byte for b in stored)
    return plain, stored, h


class CipherTests(unittest.TestCase):
    def test_keystream_vector(self):
        # First 16 keystream bytes for key 742877301; the first two words are
        # 0xf5ad3b8d and 0x325f6da0, emitted little-endian.
        self.assertEqual(E.crypt_region(b"\0" * 16, 0, 16).hex(),
                         "8d3badf5a06d5f32cd371ebcfbe40dab")

    def test_word_advances_every_four_bytes(self):
        ks = E.crypt_region(b"\0" * 8, 0, 8)
        self.assertEqual(struct.unpack("<II", ks), (0xF5AD3B8D, 0x325F6DA0))

    def test_involution_and_no_mutation(self):
        data = bytes(range(256)) * 4
        enc = E.crypt_region(data, 17, 700)
        self.assertEqual(data, bytes(range(256)) * 4)
        self.assertEqual(E.crypt_region(enc, 17, 700), data)
        self.assertEqual(enc[:17], data[:17])
        self.assertEqual(enc[700:], data[700:])

    def test_region_bounds_checked(self):
        with self.assertRaises(E.PSBError):
            E.crypt_region(b"x" * 10, 5, 11)


class RoundTripTests(unittest.TestCase):
    def test_versions_2_and_3(self):
        for version in (2, 3):
            plain, stored, h = make(version=version)
            out, rep = E.decrypt_psb(stored)
            self.assertEqual(out, plain, "version %d" % version)
            self.assertEqual(rep["version"], version)
            self.assertGreater(rep["names"], 0)
            self.assertGreater(rep["strings"], 0)
            self.assertEqual(rep["resources"], 1)
            self.assertIn("object", rep["root_keys"])
            self.assertTrue(rep["trie_tight"])
            self.assertEqual(rep["encrypted_region"], {
                "start": h.header_len, "end": h.off_choffs,
                "size": h.off_choffs - h.header_len})

    def test_only_region_changes(self):
        plain, stored, h = make()
        self.assertEqual(stored[:h.header_len], plain[:h.header_len])
        self.assertEqual(stored[h.off_choffs:], plain[h.off_choffs:])
        self.assertNotEqual(stored[h.header_len:h.off_choffs], plain[h.header_len:h.off_choffs])

    def test_wrong_key_rejected(self):
        _, stored, _ = make()
        with self.assertRaises(E.PSBError):
            E.decrypt_psb(stored, key=E.DEFAULT_KEY ^ 1)

    def test_alternate_key(self):
        plain, stored, _ = make(key=0xDEADBEEF)
        out, _ = E.decrypt_psb(stored, key=0xDEADBEEF)
        self.assertEqual(out, plain)

    def test_bad_magic(self):
        with self.assertRaises(E.PSBError):
            E.decrypt_psb(b"PBS\0" + b"\0" * 60, xp3_xor="none")

    def test_truncated_header(self):
        _, stored, _ = make()
        with self.assertRaises(E.PSBError):
            E.decrypt_psb(stored[:0x20])

    def test_encrypt_flag_rejected(self):
        plain, _, _ = make()
        b = bytearray(plain)
        struct.pack_into("<H", b, 6, 1)
        with self.assertRaises(E.PSBError):
            E.decrypt_psb(bytes(b), xp3_xor="none")

    def test_header_len_mismatch_rejected(self):
        plain, _, _ = make()
        b = bytearray(plain)
        struct.pack_into("<I", b, 8, 0x2C)
        with self.assertRaises(E.PSBError) as cm:
            E.decrypt_psb(bytes(b), xp3_xor="none")
        self.assertIn("layout", str(cm.exception))

    def test_out_of_order_offsets_rejected(self):
        plain, _, h = make()
        b = bytearray(plain)
        struct.pack_into("<I", b, 0x10, h.off_names)  # strings == names
        with self.assertRaises(E.PSBError):
            E.decrypt_psb(bytes(b), xp3_xor="none")

    def test_entries_out_of_range_rejected(self):
        plain, _, _ = make()
        b = bytearray(plain)
        struct.pack_into("<I", b, 0x24, len(plain) + 7)
        with self.assertRaises(E.PSBError):
            E.decrypt_psb(bytes(b), xp3_xor="none")

    def test_plain_magic_is_not_unwrapped(self):
        plain, _, _ = make()
        self.assertEqual(E.unwrap_xp3(plain, "auto"), (plain, 0))


class Xp3FilterTests(unittest.TestCase):
    def test_auto_unwraps(self):
        plain, stored, _ = make(xp3_byte=0x5A)
        out, rep = E.decrypt_psb(stored)
        self.assertEqual(out, plain)
        self.assertEqual(rep["xp3_filter"], 0x5A)

    def test_auto_on_plain_magic_keeps_bytes(self):
        # decrypt() is not idempotent: feeding it an already-plain PSB would
        # decrypt the region a second time.  Only the archive layer is skipped.
        plain, _, _ = make()
        data, key = E.unwrap_xp3(plain, "auto")
        self.assertEqual(key, 0)
        self.assertEqual(data, plain)

    def test_garbage_rejected(self):
        with self.assertRaises(E.PSBError):
            E.decrypt_psb(b"this is not a psb file at all", xp3_xor="auto")

    def test_none_keeps_filtered_file(self):
        _, stored, _ = make(xp3_byte=0x5A)
        with self.assertRaises(E.PSBError):
            E.decrypt_psb(stored, xp3_xor="none")


class CliTests(unittest.TestCase):
    def setUp(self):
        self.td = tempfile.TemporaryDirectory()
        self.dir = self.td.name
        self.plain, self.stored, self.h = make()
        self.src = os.path.join(self.dir, "in.psb")
        with open(self.src, "wb") as f:
            f.write(self.stored)

    def tearDown(self):
        self.td.cleanup()

    def run_main(self, argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = E.main(argv)
        return rc, out.getvalue(), err.getvalue()

    def test_decrypt_writes_plain(self):
        dst = os.path.join(self.dir, "out.psb")
        rc, out, _ = self.run_main(["decrypt", self.src, "-o", dst])
        self.assertEqual(rc, 0, out)
        with open(dst, "rb") as f:
            self.assertEqual(f.read(), self.plain)
        with open(self.src, "rb") as f:
            self.assertEqual(f.read(), self.stored)  # input untouched

    def test_refuses_same_input_and_output(self):
        with open(self.src, "rb") as f:
            before = f.read()
        rc, _, err = self.run_main(["decrypt", self.src, "-o", self.src, "--force"])
        self.assertEqual(rc, 1)
        self.assertIn("input", err)
        with open(self.src, "rb") as f:
            self.assertEqual(f.read(), before)

    def test_refuses_existing_output_without_force(self):
        dst = os.path.join(self.dir, "out.psb")
        with open(dst, "wb") as f:
            f.write(b"keep me")
        rc, _, err = self.run_main(["decrypt", self.src, "-o", dst])
        self.assertEqual(rc, 1)
        self.assertIn("exists", err)
        with open(dst, "rb") as f:
            self.assertEqual(f.read(), b"keep me")

    def test_force_replaces_output(self):
        dst = os.path.join(self.dir, "out.psb")
        with open(dst, "wb") as f:
            f.write(b"old")
        rc, _, err = self.run_main(["decrypt", self.src, "-o", dst, "--force"])
        self.assertEqual(rc, 0, err)
        with open(dst, "rb") as f:
            self.assertEqual(f.read(), self.plain)

    def test_xp3_auto_and_expect_adler(self):
        src = os.path.join(self.dir, "wrapped.psb")
        wrapped = bytes(b ^ 0x5A for b in self.stored)
        with open(src, "wb") as f:
            f.write(wrapped)
        dst = os.path.join(self.dir, "out.psb")
        adler = "%08x" % (zlib.adler32(self.stored) & 0xFFFFFFFF)
        rc, out, err = self.run_main(["decrypt", src, "-o", dst, "--expect-adler", adler])
        self.assertEqual(rc, 0, err)
        with open(dst, "rb") as f:
            self.assertEqual(f.read(), self.plain)

    def test_wrong_adler_writes_nothing(self):
        dst = os.path.join(self.dir, "out.psb")
        rc, _, err = self.run_main(["decrypt", self.src, "-o", dst, "--expect-adler", "deadbeef"])
        self.assertEqual(rc, 1)
        self.assertIn("adler32", err)
        self.assertFalse(os.path.exists(dst))

    def test_inspect_json_and_no_write(self):
        rc, out, err = self.run_main(["inspect", self.src, "--json"])
        self.assertEqual(rc, 0, err)
        rep = json.loads(out)
        self.assertEqual(rep["version"], 2)
        self.assertIn("id", rep["root_keys"])
        self.assertEqual(sorted(os.listdir(self.dir)), ["in.psb"])

    def test_inspect_rejects_wrong_key(self):
        rc, _, err = self.run_main(["inspect", self.src, "--key", "12345"])
        self.assertEqual(rc, 1)
        self.assertTrue(err.startswith("error:"))

    def test_bad_key_argument(self):
        rc, _, err = self.run_main(["inspect", self.src, "--key", "not-a-number"])
        self.assertEqual(rc, 1)
        self.assertIn("integer", err)


@unittest.skipUnless(os.path.isdir(os.path.join(HERE, "work", "samples")),
                     "no locally extracted game samples")
class RealSampleTests(unittest.TestCase):
    """Optional: real NekoPara PSBs extracted in this session (gitignored)."""

    def test_real_files_round_trip(self):
        files = sorted(glob.glob(os.path.join(HERE, "work", "samples", "*.psb")))[:4]
        files += sorted(glob.glob(os.path.join(HERE, "work", "dx", "*.psb")))[:2]
        self.assertTrue(files)
        for path in files:
            with open(path, "rb") as f:
                raw = f.read()
            plain, rep = E.decrypt_psb(raw)
            self.assertGreater(rep["names"], 0, path)
            self.assertGreater(rep["strings"], 0, path)
            self.assertIsNotNone(rep["root_keys"], path)
            data, _ = E.unwrap_xp3(raw, "auto")
            h = E.parse_header(data)
            self.assertEqual(E.crypt_region(plain, h.header_len, h.off_choffs, E.DEFAULT_KEY), data,
                             "re-encryption does not reproduce the stored file: %s" % path)


if __name__ == "__main__":
    unittest.main(verbosity=2)
