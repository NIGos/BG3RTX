"""tools/material_families/glsl.py: the GLSL normalizer and the recipe classifier.

    py -3 -m unittest tests.material_families_test

Synthetic shaders cover each class; the real-data cases (the audited pairs of PLAN_M4 "next
families") read the dumped GLSL in the main checkout's captures/alpha_cache and are skipped
where it is missing (game data is not committed).
"""
import os
import unittest
from collections import Counter
from pathlib import Path

from tools.material_families import glsl

HEAD = """#version 450
layout(set = {s0}, binding = {b0}, std140) uniform _{u}_{v}
{{
    vec4 _m0[2];
    {extra}uint _m1;
    uint _m2;
}} _{v};
layout(set = 1, binding = {bt}) uniform texture2D _{t};
layout(set = 1, binding = 2) uniform sampler _{sm};
layout(location = 0) in vec2 _{i};
layout(location = 0) out vec4 _{o0};
layout(location = 1) out vec4 _{o1};
"""
BODY = """
void main()
{{
    vec2 _{a} = fma(fract(_{i}), _{v}._m0[0].zw, _{v}._m0[0].xy);
    vec4 _{c} = texture(sampler2D(_{t}, _{sm}), _{a});
    {pre}_{o0} = vec4(_{c}.xyz, float(_{v}._m{idx}));
    _{o1} = vec4(float(_{v}._m{idx}) * 0.5, {rt1y}, 0.0, 0.0);
}}
"""


def shader(n0=10, binding=3, extra="", idx=1, rt1y="_X.y", pre="", sample="texture"):
    """A toy G-buffer FS. n0 shifts every SPIR-V id, binding moves the texture."""
    ids = dict(u=n0, v=n0 + 1, t=n0 + 2, sm=n0 + 3, i=n0 + 4, o0=n0 + 5, o1=n0 + 6, a=n0 + 7, c=n0 + 8)
    head = HEAD.format(s0=1, b0=0, bt=binding, extra=extra, **ids)
    body = BODY.format(idx=idx, rt1y=rt1y.replace("_X", f"_{ids['c']}"), pre=pre, **ids)
    if sample != "texture":
        body = body.replace("texture(sampler2D", sample)
    return head + body


class Normalizer(unittest.TestCase):
    def test_names_and_bindings_drop_out(self):
        a, b = glsl.normalize(shader(10, 3)), glsl.normalize(shader(500, 7))
        self.assertEqual(a.stmts, b.stmts)
        self.assertEqual(a.decls, b.decls)
        self.assertIn("texture2D1_0", " ".join(a.stmts))
        self.assertEqual(a.outputs, ["out0", "out1"])

    def test_value_numbering_survives_an_inserted_statement(self):
        a = glsl.normalize(shader())
        b = glsl.normalize(shader(pre="float _999 = 2.0;\n    "))
        self.assertEqual(len(set(b.stmts) - set(a.stmts)), 1)

    def test_std140_offsets(self):
        self.assertEqual(glsl.std140_offsets(["vec4 _m0[2]", "uint _m1", "uint _m2"]), [0, 32, 36])
        self.assertEqual(glsl.std140_offsets(["vec4 _m0[2]", "vec3 _m1", "uint _m2"]), [0, 32, 44])
        self.assertEqual(glsl.std140_offsets(["layout(row_major) mat4 _m0", "vec4 _m1[2]", "uint _m2"]), [0, 64, 96])


class Classifier(unittest.TestCase):
    R = {"r": shader()}

    def test_supported(self):
        self.assertEqual(glsl.classify("r", shader(), self.R).cls, "SUPPORTED")

    def test_copy_identical(self):
        v = glsl.classify("x", shader(77, 5), self.R)
        self.assertEqual((v.cls, v.term), ("COPY", "identical modulo names and bindings"))

    def test_copy_one_term(self):
        v = glsl.classify("x", shader(rt1y="0.5"), self.R)
        self.assertEqual(v.cls, "COPY")
        self.assertEqual(v.outputs, ("out1",))
        self.assertIn("-> `0.5`", v.term)

    def test_variant_fields_only(self):
        # an inserted vec3 moves the field both outputs read from byte 32 to 44: "fields only"
        v = glsl.classify("x", shader(extra="vec3 _m9;\n    ", idx=2), self.R)
        self.assertEqual(v.cls, "VARIANT")
        self.assertTrue(v.term.startswith("fields only"), v.term)
        self.assertIn(("ubo1_0", 32, 44), v.fields)

    def test_new_sampling(self):
        v = glsl.classify("x", shader(sample="textureLod(sampler2D"), self.R)
        self.assertEqual(v.cls, "NEW")


def _main_checkout() -> Path:
    root = Path(__file__).resolve().parents[1]
    return root.parents[2] if root.parent.name == "worktrees" else root


CACHE = _main_checkout() / "captures" / "alpha_cache"
KNOWN = ["8995da1e4060f53b", "14678345c10aefbc", "3da860bebbe9133b", "3df678c09f13ea60"]


@unittest.skipUnless(all((CACHE / f"{h}.glsl").exists() for h in KNOWN), "no dumped GLSL in captures/alpha_cache")
class AuditedPairs(unittest.TestCase):
    """The audit of PLAN_M4 'next families', reproduced mechanically."""

    @classmethod
    def setUpClass(cls):
        cls.src = {h: (CACHE / f"{h}.glsl").read_text(encoding="utf-8", errors="ignore") for h in KNOWN}
        cls.rec = {h: cls.src[h] for h in KNOWN[:2]}

    def test_8995_supported(self):
        self.assertEqual(glsl.classify(KNOWN[0], self.src[KNOWN[0]], self.rec).cls, "SUPPORTED")

    def test_3da860be_copy_of_8995(self):
        v = glsl.classify(KNOWN[2], self.src[KNOWN[2]], self.rec)
        self.assertEqual((v.cls, v.recipe, v.outputs), ("COPY", KNOWN[0], ("out2",)))
        self.assertIn("-> `0.5`", v.term)   # RT2.y = 0.5 instead of the material UBO's reflectance

    def test_3df678c0_copy_of_14678345(self):
        v = glsl.classify(KNOWN[3], self.src[KNOWN[3]], self.rec)
        self.assertEqual((v.cls, v.recipe, v.outputs), ("COPY", KNOWN[1], ("out2",)))
        self.assertIn("-> `0.5`", v.term)

    def test_sti_is_the_8995_fields_moved(self):
        # native_vt_surface.h: 8995 offsets 64/100/104, 14678345 0/32/36
        v = glsl.compare(glsl.Forms.of(self.src[KNOWN[0]]), glsl.Forms.of(self.src[KNOWN[1]]), KNOWN[0])
        self.assertEqual(v.cls, "VARIANT")
        moved = {(f, t) for b, f, t in v.fields if b == "ubo1_0"}
        self.assertTrue({(64, 0), (100, 32), (104, 36)} <= moved, moved)


WIND_RGB = ["14678345c10aefbc", "838318119b2f43d8", "923131c7039f33da"]


@unittest.skipUnless(all((CACHE / f"{h}.glsl").exists() for h in WIND_RGB), "no dumped GLSL in captures/alpha_cache")
class WindRgbTranslucency(unittest.TestCase):
    """native_vt_surface.h recipe 83831811 (rt_hit_material_wind_rgb): the classifier's differing statements
    against 14678345 are exactly the terms the recipe row accounts for."""

    @classmethod
    def setUpClass(cls):
        cls.src = {h: (CACHE / f"{h}.glsl").read_text(encoding="utf-8", errors="ignore") for h in WIND_RGB}
        cls.forms = {h: glsl.Forms.of(t) for h, t in cls.src.items()}

    def _only(self, a, b):
        x, y = self.forms[a].erased, self.forms[b].erased
        return Counter(y.stmts) - Counter(x.stmts), Counter(x.stmts) - Counter(y.stmts)

    def test_same_sampling_three_outputs(self):
        v = glsl.compare(self.forms[WIND_RGB[0]], self.forms[WIND_RGB[1]], WIND_RGB[0])
        self.assertEqual((v.cls, v.outputs), ("VARIANT", ("out0", "out2", "out3")))
        self.assertIn("same sampling", v.term)

    def test_terms(self):
        added, removed = self._only(WIND_RGB[0], WIND_RGB[1])
        self.assertEqual((sum(added.values()), sum(removed.values())), (5, 5))
        add = "\n".join(added)
        # out0: the N column times (gl_FrontFacing ? 1 : -1), as the built two-sided recipe 923131c7
        self.assertIn("in1 * fma(float(gl_FrontFacing), 2.0, -1.0)", add)
        two_sided = "\n".join(Counter(self.forms[WIND_RGB[2]].erased.stmts))
        self.assertIn("in1 * fma(float(gl_FrontFacing), 2.0, -1.0)", two_sided)
        # out2: RT2.w is the constant class 4 with the channel bits (no flag bit from the engine UBO)
        out2 = [s for s in added if s.startswith("out2 =")]
        self.assertEqual(len(out2), 1)
        self.assertIn("float((4u | ((ubo1_0._m? & 7u) << 4u)) | 0u)", out2[0])
        # out3: the translucency inputs (engine word / 255, 0, physical.w, 0), not read by the class 0 decode
        out3 = [s for s in added if s.startswith("out3 =")]
        self.assertEqual(len(out3), 1)
        self.assertTrue(out3[0].startswith("out3 = vec4(float(ubo1_0._m?) * 0.0039215688593685626983642578125, 0.0, clamp("))
        self.assertTrue(out3[0].rstrip().endswith(".w, 0.0, 1.0), 0.0)"))

    def test_reflectance_is_1_1_byte_20(self):
        # plain forms keep member names: RT2.y reads _m5 of the 8-float 1:1 block (std140 byte 20) where
        # 14678345 reads _m0 (byte 0); the physical sampling expressions around it are the same.
        a = [s for s in self.forms[WIND_RGB[0]].plain.stmts if s.startswith("out2 =")][0]
        b = [s for s in self.forms[WIND_RGB[1]].plain.stmts if s.startswith("out2 =")][0]
        self.assertIn("clamp(ubo1_1._m0, 0.0, 1.0)", a)
        self.assertIn("clamp(ubo1_1._m5, 0.0, 1.0)", b)
        blk = self.forms[WIND_RGB[1]].plain.blocks.get("ubo1_1", [])
        self.assertEqual(glsl.std140_offsets(blk)[5], 20)


if __name__ == "__main__":
    unittest.main()
