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

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("--dxc", required=True)
    parser.add_argument("--sharc", required=True)
    parser.add_argument("--spirv-val", help="Defaults to spirv-val beside DXC")
    parser.add_argument("--baseline", default="37299f2")
    args = parser.parse_args()
    validator = args.spirv_val or str(Path(args.dxc).with_name("spirv-val" + Path(args.dxc).suffix))
    out = ROOT / "build-native/shader-check"
    out.mkdir(parents=True, exist_ok=True)
    archive = out / "baseline.zip"
    subprocess.run(["git", "archive", "--format=zip", "--output", str(archive), args.baseline, "shaders", "common"], cwd=ROOT, check=True)
    old = out / "baseline"
    with zipfile.ZipFile(archive) as z:
        z.extractall(old)

    def compile_one(source, dest, include):
        dest.parent.mkdir(parents=True, exist_ok=True)
        first = source.read_text().splitlines()[0]
        extra = shlex.split(first[len("// dxc:"):]) if first.startswith("// dxc:") else []
        command = [args.dxc, str(source), "-spirv", "-fspv-target-env=vulkan1.3", "-HV", "2021",
                   "-T", "cs_6_6", "-E", "main", "-I", str(include), "-I", args.sharc,
                   "-D", "BG3RTX=1", "-D", "BG3RTX_SHARC_SDK=1", "-Fo", str(dest), *extra]
        p = subprocess.run(command, capture_output=True, text=True)
        if p.returncode:
            raise RuntimeError(f"{source}: {p.stdout}\n{p.stderr}")
        return hashlib.sha256(dest.read_bytes()).hexdigest()

    def check(wrapper):
        relative = wrapper.relative_to(ROOT / "shaders/native_scalp")
        current = compile_one(ROOT / "shaders" / relative, out / "current" / relative.with_suffix(".spv"), ROOT / "shaders")
        baseline = compile_one(old / "shaders" / relative, out / "old" / relative.with_suffix(".spv"), old / "shaders")
        if current != baseline:
            raise RuntimeError(f"Ordinary SPIR-V changed: {relative}")
        capable = compile_one(wrapper, out / "capable" / relative.with_suffix(".spv"), ROOT / "shaders")
        validation = subprocess.run([validator, "--target-env", "vulkan1.3", str(out / "capable" / relative.with_suffix(".spv"))], capture_output=True, text=True)
        if validation.returncode:
            raise RuntimeError(f"{wrapper}: {validation.stdout}\n{validation.stderr}")
        return {"entry": relative.as_posix(), "ordinary_sha256": current, "capable_sha256": capable}

    wrappers = sorted((ROOT / "shaders/native_scalp").rglob("*.hlsl"))
    # Two CPU compiler processes maximum, including baseline/current per entry.
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        records = list(pool.map(check, wrappers))
    result = {"baseline": args.baseline, "ordinary_identical": len(records), "capable_compiled": len(records), "capable_validated": len(records), "entries": records}
    (out / "result.json").write_text(json.dumps(result, indent=2))
    print(f"{len(records)} ordinary byte-identical; {len(records)} capable variants compile")


if __name__ == "__main__":
    main()
