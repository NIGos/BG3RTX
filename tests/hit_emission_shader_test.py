"""rt_hit_emission offline shader gate (docs/PLAN_M4_REFLECTIONS_GI.md M4.4).

1. Key off: every shader request of the baseline (shaders/**/*.hlsl + shaders/variants.txt at the
   merge-base with main) compiles to byte-identical SPIR-V in this tree: gi_trace_rt and its hit-GI
   variants, rt_reflections and its rf_hit_shade variants, D-LS and every other pass are untouched.
2. Key on: every *_em request (the hit-GI and rf_hit_shade variants plus the emissive set) compiles,
   passes spirv-val and, with bin/native_hit_{gi,rf}_bindings_test.exe built, its descriptor contract
   (common/hit_emission_bindings.h, --em).
CPU compiler tools only: no Vulkan device, no game, no queue.

    py -3 tests/hit_emission_shader_test.py [--baseline <commit>] [--jobs 8]
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

GI = ("gi/gi_trace_rt_hit_direct_em.hlsl", "gi/gi_trace_rt_sharc_hit_direct_em.hlsl",
      "gi/gi_trace_rt_hit_sun_em.hlsl", "gi/gi_trace_rt_sharc_hit_sun_em.hlsl")
RF = ("gi/rt_reflections_hit_shade_em.hlsl", "gi/rt_reflections_sharc_hit_shade_em.hlsl")
FEATURE = GI + RF


def contract(r, paths):
    file, _, d = r.partition("?")
    if file in GI:
        checker = ROOT / "bin" / "native_hit_gi_bindings_test.exe"
        flags = (["--tileset"] if "RT_NATIVE_TILESET=1" in d else []) + (["--sun"] if "_hit_sun_em" in file else []) + ["--em"]
    else:
        checker = ROOT / "bin" / "native_hit_rf_bindings_test.exe"
        flags = (["--sharc"] if "sharc" in file else []) + (["--tileset"] if "RT_NATIVE_TILESET=1" in d else []) + \
                (["--hair"] if "RT_HAIR_PHYSICAL_ORIGIN=1" in d else []) + \
                (["--cascade"] if "RT_CASCADE_MASKS=1" in d else []) + ["--em"] +                 (["--gi"] if "RF_HIT_GI=1" in d else [])
    if not checker.exists():
        return None
    c = subprocess.run([str(checker), *flags, *map(str, paths)], capture_output=True, text=True)
    return "" if c.returncode == 0 else "contract: " + c.stdout + c.stderr


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--baseline", default=default_baseline())
    ap.add_argument("--jobs", type=int, default=8)
    args = ap.parse_args()
    sharc = sharc_include()
    with tempfile.TemporaryDirectory(prefix="bg3-hem-") as tmp:
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

        checked = []

        def on(r):
            blob, err, paths = compile_one(r, ROOT / "shaders", tmp / "on", sharc)
            if blob is None:
                return r, err
            val = DXC.with_name("spirv-val.exe")
            for p in paths:
                v = subprocess.run([str(val), "--target-env", "vulkan1.3", str(p)], capture_output=True, text=True)
                if v.returncode:
                    return r, "spirv-val: " + v.stdout + v.stderr
            c = contract(r, paths)
            if c is None:
                return r, ""
            checked.append(r)
            return r, c

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
          f"{len(feature) - len(fbad)}/{len(feature)} rt_hit_emission requests compile and validate, "
          f"{len(checked)} checked against the descriptor contract")
    sys.exit(1 if bad or fbad or missing or unexpected or not feature else 0)


if __name__ == "__main__":
    main()
