import math
import os
import struct
import tempfile
import unittest
import zlib

from tools.assets import colour, compress, lsf, lsx, spirv
from tools.assets.geom import Transform, qrot
from tools.assets.lspk import Package, PackageSet
from tools.assets.resource import guid_from_lsf
from tools.assets.tests import samples


class Lz4(unittest.TestCase):
    # token 0x35: 3 literals "abc", then a match of 5 + 4 = 9 bytes at offset 3 (overlapping),
    # then the final literal-only sequence "x"
    BLOCK = bytes([0x35]) + b"abc" + b"\x03\x00" + bytes([0x10]) + b"x"
    PLAIN = b"abcabcabcabc" + b"x"

    def test_block_overlapping_match(self):
        self.assertEqual(compress.lz4_block_decompress(self.BLOCK, len(self.PLAIN)), self.PLAIN)

    def test_block_long_literal_and_match(self):
        lit = bytes(range(40))
        # 40 literals (15 + 25), then a 300-byte match at offset 40 (15 + 255 + 26 + 4)
        blk = bytes([0xFF, 25]) + lit + b"\x28\x00" + bytes([255, 26]) + bytes([0x00])
        out = compress.lz4_block_decompress(blk, 340)
        self.assertEqual(out, lit + (lit * 8)[:300])

    def test_literal_block_helper(self):
        data = os.urandom(1000)
        self.assertEqual(compress.lz4_block_decompress(samples.lz4_literal_block(data), 1000), data)

    def test_frame(self):
        self.assertEqual(compress.lz4_frame_decompress(samples.lz4_frame(self.BLOCK)), self.PLAIN)

    def test_bad_offset(self):
        with self.assertRaises(compress.DecompressError):
            compress.lz4_block_decompress(bytes([0x10]) + b"a" + b"\x05\x00", 10)

    def test_methods(self):
        data = b"hello world" * 20
        self.assertEqual(compress.decompress(1, zlib.compress(data), len(data)), data)
        self.assertEqual(compress.decompress(0, data, len(data)), data)
        try:
            from compression import zstd
        except ImportError:
            self.skipTest("no zstd in this Python")
        self.assertEqual(compress.decompress(3, zstd.compress(data), len(data)), data)


class Lspk(unittest.TestCase):
    def test_read_and_override(self):
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "Base.pak"), "wb") as f:
                f.write(samples.lspk({"a/one.txt": b"base one", "a/two.txt": b"base two"}))
            with open(os.path.join(d, "Patch8_HotFix9.pak"), "wb") as f:
                f.write(samples.lspk({"a/two.txt": b"patched two"}, priority=1))
            p = Package(os.path.join(d, "Base.pak"))
            self.assertEqual(p.version, 18)
            self.assertEqual(sorted(p.entries), ["a/one.txt", "a/two.txt"])
            self.assertEqual(p.read("a/one.txt"), b"base one")
            ps = PackageSet(d)
            self.assertEqual(ps.read("a/two.txt"), b"patched two")
            self.assertEqual(ps.read("a/one.txt"), b"base one")
            self.assertEqual(ps.names("a/*.txt"), ["a/one.txt", "a/two.txt"])

    def test_rejects_other_files(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "x.pak")
            with open(p, "wb") as f:
                f.write(b"NOPE" + b"\0" * 60)
            with self.assertRaises(ValueError):
                Package(p)


class Lsf(unittest.TestCase):
    GUID = "3a7102a7-4818-185a-9060-d72318c181f4"

    def test_parse_tree(self):
        tree = ("Templates", [], [
            ("GameObjects", [("Name", samples.T_FIXED, "Light_A"), ("Intensity", samples.T_FLOAT, 5000.0),
                             ("Shadow", samples.T_BOOL, True), ("MapKey", samples.T_GUID, self.GUID),
                             ("LightType", samples.T_UINT8, 2)],
             [("Transform", [("Position", samples.T_FVEC3, (1.0, 2.0, 3.0)),
                             ("RotationQuat", samples.T_FVEC4, (0.0, 0.0, 0.0, 1.0))], [])]),
            ("GameObjects", [("Name", samples.T_FIXED, "Light_B")], []),
        ])
        res = lsf.parse(samples.lsf(tree))
        self.assertEqual(res.version, 6)
        self.assertEqual([r.name for r in res.regions], ["Templates"])
        objs = list(res.find_all("GameObjects"))
        self.assertEqual(len(objs), 2)
        a = objs[0]
        self.assertEqual(a.get("Name"), "Light_A")
        self.assertAlmostEqual(a.get("Intensity"), 5000.0)
        self.assertIs(a.get("Shadow"), True)
        self.assertEqual(a.get("MapKey"), self.GUID)
        self.assertEqual(a.get("LightType"), 2)
        self.assertEqual(a.child("Transform").get("Position"), [1.0, 2.0, 3.0])
        self.assertIs(a.child("Transform").parent, a)

    def test_guid_byte_order(self):
        self.assertEqual(guid_from_lsf(samples.guid_bytes(self.GUID)), self.GUID)

    def test_not_lsf(self):
        with self.assertRaises(ValueError):
            lsf.parse(b"XXXX" + b"\0" * 64)


class Lsx(unittest.TestCase):
    def test_parse(self):
        xml = b"""<?xml version="1.0" encoding="utf-8"?><save><version major="4" minor="0"/>
        <region id="Config"><node id="Config">
          <attribute id="Name" type="FixedString" value="abc"/>
          <attribute id="Size" type="fvec3" value="1 2 3"/>
          <attribute id="On" type="bool" value="True"/>
          <attribute id="Count" type="int32" value="7"/>
          <children><node id="Child"><attribute id="X" type="float" value="0.5"/></node></children>
        </node></region></save>"""
        res = lsx.parse(xml)
        cfg = res.region("Config")
        self.assertEqual(cfg.get("Name"), "abc")
        self.assertEqual(cfg.get("Size"), [1.0, 2.0, 3.0])
        self.assertIs(cfg.get("On"), True)
        self.assertEqual(cfg.get("Count"), 7)
        self.assertEqual(cfg.child("Child").get("X"), 0.5)


class Spirv(unittest.TestCase):
    def test_layer_hash(self):
        # layer/util.cpp hash64: FNV-1a 64 with offset basis 1469598103934665603 (one digit
        # short of the standard 14695981039346656037; the captures are all named with it)
        self.assertEqual(spirv.fnv1a64(b""), 1469598103934665603)
        self.assertEqual(spirv.fnv1a64(b"a"), ((1469598103934665603 ^ 0x61) * 1099511628211) & (2 ** 64 - 1))
        # a runtime shader the layer named f469db7865d7c524 is checked in GameFiles

    def test_bshd_modules(self):
        vs, fs = samples.spirv_module(0), samples.spirv_module(4, "PSMain")
        mods = spirv.modules(samples.bshd(vs, fs))
        self.assertEqual([m.stage for m in mods], ["vs", "fs"])
        self.assertEqual([m.hash for m in mods], [spirv.fnv1a64(vs), spirv.fnv1a64(fs)])
        self.assertEqual(mods[1].entry, "PSMain")
        self.assertEqual(mods[0].size, len(vs))


class Geometry(unittest.TestCase):
    def test_compose_foundry_placement(self):
        # BGH_SteelWatchFoundry_B in CTY_LowerCity_A: -90 degrees about Y, then translated
        s = math.sqrt(0.5)
        foundry = Transform((-242.89, 6.58, -184.18), (0.0, -s, 0.0, s), 1.0)
        city = Transform()  # CTY_LowerCity_A sits at the origin of CTY_Main_A
        world = city.compose(foundry)
        p = world.apply((1.0, 0.0, 0.0))
        # a -90 degree yaw maps +X to +Z
        self.assertAlmostEqual(p[0], -242.89, places=4)
        self.assertAlmostEqual(p[2], -184.18 + 1.0, places=4)
        self.assertAlmostEqual(p[1], 6.58, places=5)

    def test_rotation_preserves_length(self):
        q = (0.1, 0.7, -0.2, 0.67)
        n = math.sqrt(sum(c * c for c in q))
        q = tuple(c / n for c in q)
        v = qrot(q, (3.0, 4.0, 12.0))
        self.assertAlmostEqual(math.sqrt(sum(c * c for c in v)), 13.0, places=5)

    def test_nested_scale(self):
        t = Transform((10, 0, 0), (0, 0, 0, 1), 2.0).compose(Transform((1, 1, 1), (0, 0, 0, 1), 1.0))
        self.assertEqual(t.apply((0, 0, 0)), (12.0, 2.0, 2.0))


class Colour(unittest.TestCase):
    def test_kelvin(self):
        r, g, b = colour.kelvin_to_linear_rgb(6500)
        self.assertGreater(min(r, g, b), 0.85)  # D65-ish: near white
        r, g, b = colour.kelvin_to_linear_rgb(2000)
        self.assertEqual(r, 1.0)
        self.assertLess(b, 0.15)
        r, g, b = colour.kelvin_to_linear_rgb(12000)
        self.assertEqual(b, 1.0)

    def test_srgb(self):
        self.assertAlmostEqual(colour.srgb_to_linear(1.0), 1.0)
        self.assertAlmostEqual(colour.srgb_to_linear(0.5), 0.2140, places=4)


if __name__ == "__main__":
    unittest.main()
