"""The water FS/VS pre-audit against the headers, and spirv-val of every patched water FS.

    py -3 tests/water_audit_test.py [<dir with water_compose_test's water_fs_*_patched*.spv>]

1. tools/water_audit.py --check: every common/water_fs_patch.h kFsIds row equals the ids derived from its
   dump, a mapped FS samples a shadow map only with its derived Vg (M5.4c), every common/water_policy.h
   kAudited VS passes the contract.
2. A structural change is refused: the derivation fails when the final mix is not a mix (no silent ids).
3. With the directory (CTest passes the build directory after water_compose_test wrote the modules):
   spirv-val --target-env vulkan1.3 on each patched module (M5.3, sun, local lights, refraction, and the
   three with the M5.4c game-shadow words), and
   one module of each kind per kFsIds row whose dump exists.
Skips (exit 0) without the asset DB, the dumps or the SPIR-V tools (a checkout without captures/).
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools import water_audit as wa  # noqa: E402

fails = 0


def check(what, ok):
    global fails
    print(("PASS " if ok else "FAIL ") + what)
    fails += 0 if ok else 1


dumps = wa.default_dumps()
dis, val = wa.tool("spirv-dis"), wa.tool("spirv-val")
db = ROOT / "data" / "assetdb" / "bg3rtx_assetdb.bin"
if not dumps or not dis or not val or not db.exists() or not wa.find_dump(dumps, 0x7b44a567771ecbec):
    print(f"water_audit_test: SKIP (dumps {len(dumps)}, spirv-dis {bool(dis)}, spirv-val {bool(val)}, DB {db.exists()})")
    sys.exit(0)

check("water_audit --check", wa.main(["--check"]) == 0)

_, ids = wa.header_tables()
check("kFsIds parsed (11 rows)", len(ids) == 11)
src = wa.find_dump(dumps, 0x7b44a567771ecbec)
text = subprocess.run([dis, "--raw-id", str(src)], check=True, capture_output=True, text=True).stdout
got = wa.match_fs(text)
check("7b44a567 derived as its table row", got == ids[0x7b44a567771ecbec])
broken = re.sub(r"(%4335 = OpExtInst %\d+ %1) FMix", r"\1 FMax", text)
try:
    wa.match_fs(broken)
    check("a final max instead of the mix is refused", False)
except wa.Fail:
    check("a final max instead of the mix is refused", True)
shadowed = text.replace("OpImageSampleExplicitLod", "OpImageSampleDrefExplicitLod", 1)
check("a shadow-map sample is seen", wa.fs_facts(shadowed)["dref"] == 1 and wa.fs_facts(text)["dref"] == 0)
check("an FS without a shadow map has no Vg", got["vg"] == 0)
# M5.4c: the game-shadowed families. PL_noCaustics_Shadows (S3 camp): Vg = %1365 = phi(1, max(0, pow(PCF, 2.2))).
gsrc = wa.find_dump(dumps, 0x06e66a6fbd88a6b3)
if gsrc:
    gtext = subprocess.run([dis, "--raw-id", str(gsrc)], check=True, capture_output=True, text=True).stdout
    g = wa.match_fs(gtext)
    check("06e66a6f derived as its table row, Vg %1365", g == ids[0x06e66a6fbd88a6b3] and g["vg"] == 1365)
    check("06e66a6f samples the cascade 7 times", wa.fs_facts(gtext)["dref"] == 7)
    m = re.search(r"%1365 = OpPhi (%\d+) (%\d+) ", gtext)
    one = m.group(2) if m else None
    zero = re.search(r"%(\d+) = OpConstant " + (m.group(1) if m else "%0") + r" 0\n", gtext)
    if one and zero:   # the cascade-off branch's 1 replaced by 0: Vg is no longer a visibility factor
        nb = gtext.replace(f"%1365 = OpPhi {m.group(1)} {one} ", f"%1365 = OpPhi {m.group(1)} %{zero.group(1)} ", 1)
        try:
            wa.match_fs(nb)
            check("a Vg phi without its 1 is refused", False)
        except wa.Fail:
            check("a Vg phi without its 1 is refused", True)
    # A shadow-map sample that reaches the colour outside Vg is refused: the colour mix weight a scaled by it.
    first = re.search(r"%(\d+) = OpImageSampleDref", gtext).group(1)
    a_id = ids[0x06e66a6fbd88a6b3]["a"]
    am = re.search(rf"%{a_id} = OpFMul (%\d+) (%\d+) (%\d+)\n", gtext)
    if am:
        leak = gtext.replace(am.group(0), f"%{a_id} = OpFMul {am.group(1)} {am.group(2)} %{first}\n", 1)
        try:
            wa.match_fs(leak)
            check("a shadow-map sample outside Vg is refused", False)
        except wa.Fail:
            check("a shadow-map sample outside Vg is refused", True)
    else:
        check("06e66a6f opacity is an OpFMul (the leak test)", False)
else:
    print("no dump of 06e66a6f: its Vg derivation is not exercised")

if len(sys.argv) > 1:
    out = Path(sys.argv[1])
    for fs in ids:
        if not wa.find_dump(dumps, fs):
            print(f"no dump of {fs:016x}: its patched modules are not validated")
            continue
        for kind in ("patched", "patched_sun", "patched_ls", "patched_refr", "patched_sun_gsh", "patched_ls_gsh",
                     "patched_refr_gsh"):
            p = out / f"water_fs_{fs >> 32:08x}_{kind}.spv"
            if not p.exists():
                check(f"{p.name} written by water_compose_test", False)
                continue
            r = subprocess.run([val, "--target-env", "vulkan1.3", str(p)], capture_output=True, text=True)
            check(f"spirv-val {p.name}" + ("" if r.returncode == 0 else ": " + (r.stderr or r.stdout).strip()[:300]),
                  r.returncode == 0)

print(f"water_audit_test: {'FAIL' if fails else 'PASS'} ({fails} failures)")
sys.exit(1 if fails else 0)
