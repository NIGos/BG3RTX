import os
import tempfile
import unittest

from tools.assets import dbformat
from tools.assets.extract import lights as lx
from tools.assets.extract import materials as mx
from tools.assets.resource import Node
from tools.assets.tests import samples

GAME = os.environ.get("BG3_DIR", r"D:\SteamLibrary\steamapps\common\Baldurs Gate 3")


def node(name, attrs=None, children=()):
    n = Node(name)
    n.attrs.update(attrs or {})
    for c in children:
        c.parent = n
        n.children.append(c)
    return n


class Format(unittest.TestCase):
    def test_record_sizes(self):
        for tag, rec in dbformat.RECORDS.items():
            self.assertEqual(rec.size, dbformat.EXPECTED_SIZES[tag], tag)
            self.assertEqual(rec.size % 4, 0, tag)

    def test_roundtrip(self):
        w = dbformat.Writer()
        name = w.strings.add("CTY_Main_A")
        self.assertEqual(w.strings.add("CTY_Main_A"), name)
        w.add_records("ESHD", [(0x1234, name, 5)])
        w.add_records("TPTS", [(1.0, 2.0), (3.0, 4.0)])
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "db.bin")
            sizes = w.write(p, b"0123456789abcdef", build_time=1)
            with open(p, "rb") as f:
                data = f.read()
        r = dbformat.Reader(data)
        self.assertEqual(r.rows("ESHD"), [(0x1234, name, 5)])
        self.assertEqual(r.rows("TPTS"), [(1.0, 2.0), (3.0, 4.0)])
        self.assertEqual(r.string(name), "CTY_Main_A")
        self.assertEqual(r.digest, b"0123456789abcdef")
        for tag, (off, _s, _c, _r) in r.toc.items():
            self.assertEqual(off % 16, 0, tag)
        self.assertEqual(sizes["TPTS"]["count"], 2)

    def test_truncated(self):
        w = dbformat.Writer()
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "db.bin")
            w.write(p)
            with open(p, "rb") as f:
                data = f.read()
        with self.assertRaises(ValueError):
            dbformat.Reader(data[:-1])


class LightTemplates(unittest.TestCase):
    def test_resolution_order(self):
        class R(lx.TemplateResolver):
            def __init__(self, t):
                self.t, self.missing = t, __import__("collections").Counter()
        parent = node("GameObjects", {"MapKey": "P", "Intensity": 100.0, "Radius": 7.0, "LightType": 1})
        child = node("GameObjects", {"MapKey": "C", "ParentTemplateId": "P", "Radius": 3.0})
        res = R({"P": parent, "C": child})
        placed = node("GameObjects", {"TemplateName": "C", "Intensity": 9.0})
        self.assertEqual(lx.resolve(placed, res, "Intensity"), (9.0, True))   # placement wins
        self.assertEqual(lx.resolve(placed, res, "Radius"), (3.0, True))      # nearest template
        self.assertEqual(lx.resolve(placed, res, "LightType"), (1, True))     # inherited
        self.assertEqual(lx.resolve(placed, res, "Kelvin"), (lx.DEFAULTS["Kelvin"], False))


class MaterialTemplate(unittest.TestCase):
    def test_bindings_and_class(self):
        def binding(name, s, b):
            return (name, [("VkDescriptorSet", samples.T_INT, s), ("VkBindingIndex", samples.T_INT, b)], [])
        tree = ("Material", [("ShadingModel", samples.T_UINT8, 0), ("BlendStateID", samples.T_UINT8, 7)], [
            ("ShaderDescriptions", [], [
                ("ShaderDesc_StaticDeferred", [("EngineCBSize", samples.T_INT, 112), ("MaterialCBSize", samples.T_INT, 16),
                                               ("VirtualTexture_TilesetDataIndex", samples.T_INT, 100)],
                 [binding("EngineCBBinding", 1, 0), binding("MaterialCBSize", 1, 1)])]),
            ("ScalarParameters", [("ParameterName", samples.T_FIXED, "GlowMultiplier"), ("Value", samples.T_FLOAT, 2.0),
                                  ("StaticDeferred", samples.T_INT, 12)], []),
            ("ScalarParameters", [("ParameterName", samples.T_FIXED, "Blood"), ("StaticDeferred", samples.T_INT, 4)], []),
            ("Texture2DParameters", [("ParameterName", samples.T_FIXED, "normalmap")], [binding("StaticDeferred", 1, 3)]),
            ("Texture2DParameters", [("ParameterName", samples.T_FIXED, "Glowmap")], [binding("StaticDeferred", 1, 2)]),
        ])
        tpl, cbs, pbv = mx.parse_template("Public/Shared/Assets/Materials/Test/T_GM.lsf", samples.lsf(tree))
        self.assertEqual(tpl.name, "Test/T_GM")
        self.assertEqual(tpl.cls, "emissive")
        self.assertTrue(tpl.flags & mx.F_OPAQUE and tpl.flags & mx.F_EMISSIVE)
        self.assertEqual(cbs["StaticDeferred"], ((1, 0, 112), (1, 1, 16)))
        roles = {p.role: p for p in pbv["StaticDeferred"]}
        self.assertEqual((roles["emissive_intensity"].binding, roles["emissive_intensity"].offset), (1, 12))
        self.assertEqual(roles["emissive_intensity"].default[0], 2.0)
        self.assertEqual(roles["normal_map"].binding, 3)
        self.assertEqual(roles["emissive_map"].binding, 2)
        self.assertEqual(roles["vt_tileset_index"].offset, 100)
        self.assertNotIn("other", roles)  # Blood has no role and is not kept

    def test_variant_names(self):
        self.assertEqual(len(mx.VARIANTS), 16)
        self.assertEqual(mx.VARIANTS[mx.VARIANT_IDS["StaticInstancedDeferred"]], "StaticInstancedDeferred")


@unittest.skipUnless(os.path.isdir(os.path.join(GAME, "Data")), "BG3 install not found (set BG3_DIR)")
class GameFiles(unittest.TestCase):
    """Known facts of the Patch 8 install (read-only)."""

    @classmethod
    def setUpClass(cls):
        from tools.assets.lspk import PackageSet
        cls.ps = PackageSet(os.path.join(GAME, "Data"), only=["Materials.pak", "EngineShaders.pak", "Gustav.pak"])

    def test_base_vt_bindings(self):
        p = "Public/Shared/Assets/Materials/Base/Base_VT.lsf"
        tpl, cbs, pbv = mx.parse_template(p, self.ps.read(p))
        self.assertEqual(tpl.cls, "opaque")
        roles = {x.role: x for x in pbv["StaticInstancedDeferred"]}
        # AUDIT_ENGINE_DATA 2.1: b3 page table, b5 base colour, b4 normal, b6 physical
        self.assertEqual((roles["vt_table"].set, roles["vt_table"].binding), (1, 3))
        self.assertEqual(roles["vt_albedo"].binding, 5)
        self.assertEqual(roles["vt_normal"].binding, 4)
        self.assertEqual(roles["vt_physical"].binding, 6)

    def test_lighting_shader_hash(self):
        from tools.assets import spirv
        mods = spirv.modules(self.ps.read("Shaders/Vulkan/ClusteredLighting/ClusteredLighting_0.bshd"))
        self.assertIn("f469db7865d7c524", [f"{m.hash:016x}" for m in mods])

    def test_foundry_lights(self):
        from tools.assets.lsf import parse
        r = parse(self.ps.read("Mods/GustavDev/Levels/CTY_LowerCity_A/Lights/_merged.lsf"))
        self.assertEqual(sum(1 for _ in r.find_all("GameObjects")), 924)  # AUDIT_ENGINE_DATA 2.3


if __name__ == "__main__":
    unittest.main()
