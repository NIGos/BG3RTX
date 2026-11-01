"""ls_k offline shader gate (docs/TRACK_D_LOCAL_LIGHTS.md 9.11, exact-k + RIS remainder).

1. Key off: every shader request of the baseline (shaders/**/*.hlsl + shaders/variants.txt at the
   merge-base with main) compiles to byte-identical SPIR-V in this tree: rt_local_shadows and its
   box/full/check/reference variants and every other pass are untouched (LS_K=0 compiles none of it).
2. Key on: every LS_K request (gi/rt_local_shadows{,_box,_full,_full_box}_k.hlsl with its flags)
   compiles and passes spirv-val.
CPU compiler tools only: no Vulkan device, no game, no queue.

    py -3 tests/ls_exact_k_shader_test.py [--baseline <commit>] [--jobs 8]
"""
import argparse
import concurrent.futures
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from shader_baseline import default_baseline  # noqa: E402
from rf_hit_shade_shader_test import DXC, ROOT, compile_one, requests, sharc_include  # noqa: E402

FEATURE = ("gi/rt_local_shadows_k.hlsl", "gi/rt_local_shadows_box_k.hlsl", "gi/rt_local_shadows_full_k.hlsl",
           "gi/rt_local_shadows_full_box_k.hlsl")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--baseline", default=default_baseline())
    ap.add_argument("--jobs", type=int, default=8)
    args = ap.parse_args()
    sharc = sharc_include()
    with tempfile.TemporaryDirectory(prefix="bg3-lsk-") as tmp:
        tmp = Path(tmp)
        archive = tmp / "baseline.zip"
        subprocess.run(["git", "archive", "--format=zip", "--output", str(archive), args.baseline, "shaders", "common"],
                       cwd=ROOT, check=True)
        base = tmp / "base"
        with zipfile.ZipFile(archive) as z:
            z.extractall(base)
        old_list = requests(base / "shaders")
        new_list = requests(ROOT / "shaders")
        missing = [r for r in old_list if r not in new_list]
        feature = [r for r in new_list if r.partition("?")[0] in FEATURE]
        # rf_hit_gi requests (RF_HIT_GI=1) are tests/rf_hit_shade_shader_test.py's feature, compiled there.
        unexpected = [r for r in new_list if r not in old_list and r not in feature and "RF_HIT_GI=1" not in r]
        for d in ("old", "new", "on"):
            (tmp / d).mkdir()

        def same(r):
            a, ea, _ = compile_one(r, base / "shaders", tmp / "old", sharc)
            b, eb, _ = compile_one(r, ROOT / "shaders", tmp / "new", sharc)
            if a is None or b is None:
                return r, "compile", ea or eb
            return r, "same" if a == b else "DIFFERENT", ""

        def on(r):
            blob, err, paths = compile_one(r, ROOT / "shaders", tmp / "on", sharc)
            if blob is None:
                return r, err
            val = DXC.with_name("spirv-val.exe")
            for p in paths:
                v = subprocess.run([str(val), "--target-env", "vulkan1.3", str(p)], capture_output=True, text=True)
                if v.returncode:
                    return r, "spirv-val: " + v.stdout + v.stderr
            return r, ""

        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            identity = list(pool.map(same, old_list))
            feature_res = list(pool.map(on, feature))
    bad = [x for x in identity if x[1] != "same"]
    fbad = [x for x in feature_res if x[1]]
    for r, state, err in bad:
        print(f"{state}: {r}\n{err}")
    for r, err in fbad:
        print(f"FEATURE FAIL: {r}\n{err}")
    for r in missing:
        print(f"MISSING request (in the baseline, not here): {r}")
    for r in unexpected:
        print(f"UNEXPECTED new request: {r}")
    print(f"baseline {args.baseline[:12]}: {len(identity) - len(bad)}/{len(identity)} key-off requests byte-identical; "
          f"{len(feature) - len(fbad)}/{len(feature)} ls_k (LS_K) requests compile and validate")
    sys.exit(1 if bad or fbad or missing or unexpected or not feature else 0)


if __name__ == "__main__":
    main()
