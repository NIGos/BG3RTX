"""Compile every compute shader offline, as layer/shaders.cpp does at run time (no game).

    py -3 tools/compile_shaders.py [files relative to shaders/ ...]

DXC from the Vulkan SDK, -spirv vulkan1.3, HLSL 2021, cs_6_6, -O3, the SHaRC headers (this
checkout's external/, else the main checkout's) and a first line "// dxc: <arguments>".
Without arguments: every shaders/**/*.hlsl. Exit code 1 if any fails.
"""
import os
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

root = Path(__file__).resolve().parent.parent
shaders = root / "shaders"
dxc = Path(os.environ.get("VULKAN_SDK", r"C:\VulkanSDK\1.4.357.0")) / "Bin" / "dxc.exe"
sharc = []
for c in (root / "external" / "sharc-1.8.3" / "include", root.parents[2] / "external" / "sharc-1.8.3" / "include"):
    if (c / "SharcCommon.h").exists():
        sharc = ["-I", str(c), "-D", "BG3RTX_SHARC_SDK=1"]
        break
files = sys.argv[1:] or sorted(str(p.relative_to(shaders)).replace("\\", "/") for p in shaders.rglob("*.hlsl"))


def build(f, tmp):
    src = shaders / f
    text = src.read_text(errors="replace")
    first = text.split("\n", 1)[0].strip()
    extra = first[len("// dxc:"):].split() if first.startswith("// dxc:") else []
    # Every [numthreads] entry (rr_post.hlsl has several), else main.
    entries = re.findall(r"\[numthreads\([^)]*\)\]\s*void\s+(\w+)", text) or ["main"]
    rc, msgs = 0, []
    for e in entries:
        out = Path(tmp) / (f.replace("/", "_") + f".{e}.spv")
        r = subprocess.run([str(dxc), str(src), "-spirv", "-fspv-target-env=vulkan1.3", "-HV", "2021", "-T", "cs_6_6",
                            "-E", e, "-I", str(shaders), "-D", "BG3RTX=1", "-O3"] + sharc + extra + ["-Fo", str(out)],
                           capture_output=True, text=True)
        rc |= r.returncode
        if r.returncode:
            msgs.append(f"[{e}] " + (r.stdout + r.stderr).strip())
    return f, rc, "\n".join(msgs)


with tempfile.TemporaryDirectory() as tmp, ThreadPoolExecutor(8) as pool:
    res = list(pool.map(lambda f: build(f, tmp), files))
    # These production GI variants need their dedicated host interface, not
    # the contracted local-shadow layout. Check the actual compiled modules.
    material_gi = [str(Path(tmp) / (f.replace("/", "_") + ".main.spv"))
                   for f, rc, _ in res if not rc and f.endswith(
                       ("gi_trace_rt_hit_direct.hlsl", "gi_trace_rt_sharc_hit_direct.hlsl"))]
    checker = root / "bin" / "native_hit_gi_bindings_test.exe"
    if material_gi and checker.exists():
        result = subprocess.run([str(checker), *material_gi], capture_output=True, text=True)
        if result.returncode:
            res.append(("GI host descriptor interface", result.returncode, result.stdout + result.stderr))
        else:
            print(f"GI host descriptor interface checked: {len(material_gi)} actual SPIR-V modules")
    elif material_gi:
        print("SKIP GI host descriptor interface: build native_hit_gi_bindings_test first; shader compilation alone is not interface validation")
bad = [r for r in res if r[1]]
for f, rc, msg in res:
    print(f"{'ok  ' if not rc else 'FAIL'} {f}")
    if rc:
        print(msg[:4000])
print(f"{len(res) - len(bad)}/{len(res)} compiled" + ("" if sharc else " (no SHaRC SDK found)"))
sys.exit(1 if bad else 0)
