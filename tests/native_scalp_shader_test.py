"""CPU-only DXC validation: all opt-in wrappers and ordinary SPIR-V identity.

Usage: py -3 tests/native_scalp_shader_test.py --dxc PATH --sharc PATH
Outputs remain under build-native/shader-check. No Vulkan/device calls.
"""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import zipfile

from shader_baseline import default_baseline

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("--dxc", required=True)
    parser.add_argument("--sharc", required=True)
    parser.add_argument("--spirv-val", help="Defaults to spirv-val beside DXC")
    parser.add_argument("--baseline", default=default_baseline(), help="Commit of the key-off reference (default: merge-base with main)")
    args = parser.parse_args()
    validator = args.spirv_val or str(Path(args.dxc).with_name("spirv-val" + Path(args.dxc).suffix))
    out = ROOT / "build-native/shader-check"
    out.mkdir(parents=True, exist_ok=True)
    archive = out / "baseline.zip"
    subprocess.run(["git", "archive", "--format=zip", "--output", str(archive), args.baseline, "shaders", "common"], cwd=ROOT, check=True)
    old = out / "baseline"
    with zipfile.ZipFile(archive) as z:
        z.extractall(old)

    def compile_one(request, dest, include):
        # request: "<file>?NAME=VALUE;..." (common/shader_variant.h), the defines as -D.
        file, _, defs = request.partition("?")
        source = include / file
        dest.parent.mkdir(parents=True, exist_ok=True)
        first = source.read_text().splitlines()[0]
        extra = shlex.split(first[len("// dxc:"):]) if first.startswith("// dxc:") else []
        command = [args.dxc, str(source), "-spirv", "-fspv-target-env=vulkan1.3", "-HV", "2021",
                   "-T", "cs_6_6", "-E", "main", "-I", str(include), "-I", args.sharc,
                   "-D", "BG3RTX=1", "-D", "BG3RTX_SHARC_SDK=1",
                   *[a for d in defs.split(";") if d for a in ("-D", d)], "-Fo", str(dest), *extra]
        p = subprocess.run(command, capture_output=True, text=True)
        if p.returncode:
            raise RuntimeError(f"{request}: {p.stdout}\n{p.stderr}")
        return hashlib.sha256(dest.read_bytes()).hexdigest()

    scalp = ["RT_NATIVE_SCALP=1", "RT_NATIVE_HAIR=1"]

    def ordinary(request):
        file, _, defs = request.partition("?")
        rest = [d for d in defs.split(";") if d not in scalp]
        return file + ("?" + ";".join(rest) if rest else "")

    def check(wrapper):
        relative = ordinary(wrapper)
        name = Path(wrapper.partition("?")[0] + "." + hashlib.sha1(wrapper.encode()).hexdigest()[:12] + ".spv")
        current = compile_one(relative, out / "current" / name, ROOT / "shaders")
        baseline = compile_one(relative, out / "old" / name, old / "shaders")
        if current != baseline:
            raise RuntimeError(f"Ordinary SPIR-V changed: {relative}")
        capable = compile_one(wrapper, out / "capable" / name, ROOT / "shaders")
        validation = subprocess.run([validator, "--target-env", "vulkan1.3", str(out / "capable" / name)], capture_output=True, text=True)
        if validation.returncode:
            raise RuntimeError(f"{wrapper}: {validation.stdout}\n{validation.stderr}")
        return {"entry": relative, "ordinary_sha256": current, "capable_sha256": capable}

    # The old shaders/native_scalp/ wrappers: the scalp alone or with the cascade masks.
    wrappers = [l for l in (ROOT / "shaders/variants.txt").read_text().splitlines()
                if "?" in l and all(d in scalp or d == "RT_CASCADE_MASKS=1" or (d == "RT_CASCADE_SUN=1" and l.startswith("gi/rt_shadow.hlsl?"))
                                    for d in l.partition("?")[2].split(";")) and scalp[0] in l]
    # Two CPU compiler processes maximum, including baseline/current per entry.
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        records = list(pool.map(check, wrappers))
    result = {"baseline": args.baseline, "ordinary_identical": len(records), "capable_compiled": len(records), "capable_validated": len(records), "entries": records}
    (out / "result.json").write_text(json.dumps(result, indent=2))
    print(f"{len(records)} ordinary byte-identical; {len(records)} capable variants compile")


if __name__ == "__main__":
    main()
