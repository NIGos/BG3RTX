"""The water FS/VS pre-audit against the headers, and spirv-val of every patched water FS.

    py -3 tests/water_audit_test.py [<dir with water_compose_test's water_fs_*_patched*.spv>]

1. tools/water_audit.py --check: every common/water_fs_patch.h kFsIds row equals the ids derived from its
   dump, no mapped FS samples a shadow map, every common/water_policy.h kAudited VS passes the contract.
2. A structural change is refused: the derivation fails when the final mix is not a mix (no silent ids).
3. With the directory (CTest passes the build directory after water_compose_test wrote the modules):
   spirv-val --target-env vulkan1.3 on each patched module (M5.3, sun, local lights, refraction), and
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
check("kFsIds parsed (9 rows)", len(ids) == 9)
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

if len(sys.argv) > 1:
    out = Path(sys.argv[1])
    for fs in ids:
        if not wa.find_dump(dumps, fs):
            print(f"no dump of {fs:016x}: its patched modules are not validated")
            continue
        for kind in ("patched", "patched_sun", "patched_ls", "patched_refr"):
            p = out / f"water_fs_{fs >> 32:08x}_{kind}.spv"
            if not p.exists():
                check(f"{p.name} written by water_compose_test", False)
                continue
            r = subprocess.run([val, "--target-env", "vulkan1.3", str(p)], capture_output=True, text=True)
            check(f"spirv-val {p.name}" + ("" if r.returncode == 0 else ": " + (r.stderr or r.stdout).strip()[:300]),
                  r.returncode == 0)

print(f"water_audit_test: {'FAIL' if fails else 'PASS'} ({fails} failures)")
sys.exit(1 if fails else 0)
