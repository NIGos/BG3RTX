"""scripts/material_table.py on the reference G-buffer shader 14678345 (Base_VT, docs/AUDIT_ENGINE_DATA.md
section 0: b5 base colour via the indirection's .x, b6 physical via .z: RT2.x = physical.y,
RT2.z = physical.x, RT2.y = the material UBO's reflectance). Needs the shader dump (a capture
run with dump_shaders=1) and spirv-cross; skipped without them.

    py -3 tests/material_table_test.py
"""
import pathlib
import shutil
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
MAIN = ROOT.parents[2] if ROOT.parent.name == "worktrees" else ROOT
sys.path.insert(0, str(ROOT / "scripts"))
import material_table as mt  # noqa: E402


def main():
    spv = None
    for base in (ROOT, MAIN):
        p = base / "captures" / "shaders" / "14678345c10aefbc.spv"
        if p.exists():
            spv = p
            break
    tool = mt.spirv_cross()
    if not spv or not (shutil.which("spirv-cross") or pathlib.Path(tool).exists()):
        print("SKIP: no shader dump or spirv-cross")
        return 0
    fails = 0
    with tempfile.TemporaryDirectory() as d:
        dec = mt.decompile(tool, spv, pathlib.Path(d))
        assert dec, "not recognised as a G-buffer fragment shader"
        line, why = mt.classify("14678345c10aefbc", *dec)
        print(line)
        want = ["14678345c10aefbc vt uv=0 ubo=1.0 p0=0 idx=32 ssbo=0.12 ind=1.3",
                "alb=vt:1.5:0:0:xyz", "rough=vt:1.6:2:2:y", "metal=vt:1.6:2:2:x", "refl=u:1.1:0"]
        for w in want:
            if w not in (line or ""):
                print("FAIL: missing %r" % w)
                fails += 1
    # The resolver's building blocks on small expressions.
    sh = mt.Shader("t", "void main()\n{\n    vec3 _5 = vec3(_1.x, 0.5, 2.0);\n    _9 = vec4(_5, 1.0);\n}\n",
                   {"outputs": [{"name": "_9", "location": 1}], "inputs": [], "types": {}})
    for c, want in ((1, ("c", 0.5)), (2, ("c", 2.0)), (3, ("c", 1.0))):
        got = sh.output(1, c)
        if got != want:
            print("FAIL: component %d: %r != %r" % (c, got, want))
            fails += 1
    if sh.output(1, 0)[0] != "?":
        print("FAIL: an undeclared variable must stay unknown")
        fails += 1
    print("OK" if not fails else "%d failures" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
