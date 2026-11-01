"""rf_hit_shade offline shader gate (docs/PLAN_M4_REFLECTIONS_GI.md M4.2).

1. Key off: every shader request of the baseline (shaders/**/*.hlsl + shaders/variants.txt at the
   merge-base with main) compiles to byte-identical SPIR-V in this tree. That covers rt_reflections
   and its SHaRC variant, D-LS (bg3_sun.hlsli's set macro) and gi_hit_direct (the shared hit
   shading include): the default path and the existing feature paths are untouched.
2. Key on: every rt_reflections*_hit_shade request compiles and passes spirv-val; with
   bin/native_hit_rf_bindings_test.exe built, each module is checked against its descriptor contract.
CPU compiler tools only: no Vulkan device, no game, no queue.

    py -3 tests/rf_hit_shade_shader_test.py [--baseline <commit>] [--jobs 8]
"""
import argparse
import concurrent.futures
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from shader_baseline import default_baseline  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
DXC = Path(os.environ.get("VULKAN_SDK", r"C:\VulkanSDK\1.4.357.0")) / "Bin" / "dxc.exe"
FEATURE = ("gi/rt_reflections_hit_shade.hlsl", "gi/rt_reflections_sharc_hit_shade.hlsl")


def requests(shaders):
    files = sorted(str(p.relative_to(shaders)).replace("\\", "/") for p in shaders.rglob("*.hlsl"))
    return files + [l.strip() for l in (shaders / "variants.txt").read_text().splitlines()
                    if l.strip() and not l.startswith("#")]


def sharc_include():
    for c in (ROOT / "external" / "sharc-1.8.3" / "include", ROOT.parents[2] / "external" / "sharc-1.8.3" / "include"):
        if (c / "SharcCommon.h").exists():
            return ["-I", str(c), "-D", "BG3RTX_SHARC_SDK=1"]
    return []


def compile_one(request, shaders, out_dir, sharc):
    """Every entry of the request, concatenated (as tools/compile_shaders.py compiles them)."""
    import re
    file, _, defs = request.partition("?")
    src = shaders / file
    text = src.read_text(errors="replace")
    first = text.split("\n", 1)[0].strip()
    extra = shlex.split(first[len("// dxc:"):]) if first.startswith("// dxc:") else []
    entries = re.findall(r"\[numthreads\([^)]*\)\]\s*void\s+(\w+)", text) or ["main"]
    blob, paths = b"", []
    for e in entries:
        out = out_dir / (file.replace("/", "_") + "." + hashlib.sha1(request.encode()).hexdigest()[:12] + f".{e}.spv")
        r = subprocess.run([str(DXC), str(src), "-spirv", "-fspv-target-env=vulkan1.3", "-HV", "2021", "-T", "cs_6_6",
                            "-E", e, "-I", str(shaders), "-D", "BG3RTX=1", "-O3", *sharc,
                            *[a for d in defs.split(";") if d for a in ("-D", d)], *extra, "-Fo", str(out)],
                           capture_output=True, text=True)
        if r.returncode:
            return None, request + "\n" + (r.stdout + r.stderr)[:3000], paths
        blob += out.read_bytes()
        paths.append(out)
    return blob, "", paths


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--baseline", default=default_baseline())
    ap.add_argument("--jobs", type=int, default=8)
    args = ap.parse_args()
    sharc = sharc_include()
    with tempfile.TemporaryDirectory(prefix="bg3-rfhs-") as tmp:
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
        # rf_hit_gi (RF_HIT_GI=1) requests of every hit-shade file, emission twins included, are new feature requests.
        feature = [r for r in new_list if r.partition("?")[0] in FEATURE or "RF_HIT_GI=1" in r.partition("?")[2]]
        unexpected = [r for r in new_list if r not in old_list and r not in feature]
        (tmp / "old").mkdir()
        (tmp / "new").mkdir()
        (tmp / "on").mkdir()

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
            checker = ROOT / "bin" / "native_hit_rf_bindings_test.exe"
            if checker.exists():
                d = r.partition("?")[2]
                flags = (["--sharc"] if "sharc" in r.partition("?")[0] else []) + \
                        (["--tileset"] if "RT_NATIVE_TILESET=1" in d else []) + \
                        (["--hair"] if "RT_HAIR_PHYSICAL_ORIGIN=1" in d else []) +                         (["--cascade"] if "RT_CASCADE_MASKS=1" in d else []) +                         (["--em"] if r.partition("?")[0].endswith("_em.hlsl") else []) +                         (["--gi"] if "RF_HIT_GI=1" in d else [])
                c = subprocess.run([str(checker), *flags, *map(str, paths)], capture_output=True, text=True)
                if c.returncode:
                    return r, "contract: " + c.stdout + c.stderr
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
          f"{len(feature) - len(fbad)}/{len(feature)} rf_hit_shade requests compile, validate"
          f"{' and match the contract' if (ROOT / 'bin' / 'native_hit_rf_bindings_test.exe').exists() else ''}")
    sys.exit(1 if bad or fbad or missing or unexpected or not feature else 0)


if __name__ == "__main__":
    main()
