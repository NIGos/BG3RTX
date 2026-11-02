"""common/native_hit_generated.h is what tools/material_families/recipe_rows.py derives from the asset DB, the dumped
shaders and the audited recipes (rt_hit_material_generated). Skips when the asset DB or the shader dumps are absent
(a fresh checkout); the C++ pins are tests/native_hit_generated_test.cpp."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.material_families import cli, recipe_rows  # noqa: E402


class Generated(unittest.TestCase):
    def test_terms(self):
        self.assertTrue(recipe_rows.FLAG_TERM.match("out2: `(0u | uint(int(ubo1_0._m3 != 0u)<< 3))` -> `0u`"))
        self.assertTrue(recipe_rows.FLAG_TERM.match("out2: `(0u | uint(int(ubo1_0._m ? != 0u)<< 3))` -> `0u`"))
        self.assertTrue(recipe_rows.CONST_TERM.match("out2: `clamp(ubo1_1._m0, 0.0, 1.0)` -> `0.5`; decls: -ubo1_1"))
        self.assertTrue(recipe_rows.REFL_TERM.match("out2: `_m3` -> `_m1`; decls: ~ubo1_1"))
        self.assertFalse(recipe_rows.REFL_TERM.match("out2: `_m3` -> `0.5`"))

    def test_db_fields(self):
        p = [("vt_uv_transform", "engine_cb", 1, 0, 0), ("vt_tileset_index", "engine_cb", 1, 0, 44),
             ("light_channel", "engine_cb", 1, 0, 48), ("reflectance", "scalar", 1, 1, 8), ("vt_table", "vt_table", 1, 3, 0),
             ("vt_normal", "vt_cache", 1, 4, 0), ("vt_albedo", "vt_cache", 1, 5, 0), ("vt_physical", "vt_cache", 1, 6, 0)]
        f = recipe_rows.db_fields(p, 16)
        self.assertEqual((f["transform"], f["tileset"], f["channel"], f["reflectance"], f["layout"]), (0, 44, 48, 8, "LayoutBase"))
        vt4 = [x if x[0] not in ("vt_table", "vt_normal", "vt_albedo", "vt_physical") else x[:3] + (x[3] + 1, 0) for x in p]
        self.assertEqual(recipe_rows.db_fields(vt4, 16)["layout"], "LayoutVt4")
        self.assertIsNone(recipe_rows.db_fields(p[1:], 16))   # no UV transform: not a VT recipe

    def test_header_current(self):
        if not cli.DB.exists() or not any(d.exists() for d in cli.GLSL_DIRS + cli.SPV_DIRS):
            self.skipTest("no asset DB or shader dumps")
        self.assertEqual(recipe_rows.main(["--check"]), 0)


if __name__ == "__main__":
    unittest.main()
