"""Driver statistics (registers, shared memory, binary size) of compute shaders, for occupancy work.

    py -3 tools/pipeline_stats.py [--shaders DIR] gi/rt_local_shadows.hlsl [...]

Compiles each file as layer/shaders.cpp does (DXC from the Vulkan SDK, cs_6_6, -O3),
reflects its descriptors with spirv-cross and creates the pipeline on this machine's GPU
with bin/pipeline_stats.exe (tools/pipeline_stats.cpp). Needs no game.
"""
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parent.parent
sdk = Path(os.environ.get("VULKAN_SDK", r"C:\VulkanSDK\1.4.357.0")) / "Bin"
args = sys.argv[1:]
shaders = root / "shaders"
if args[:1] == ["--shaders"]:
    shaders, args = Path(args[1]), args[2:]
# VkDescriptorType numbers
kind = {"separate_images": 2, "separate_samplers": 0, "images": 3, "ssbos": 7, "ubos": 6,
        "acceleration_structures": 1000150000}
# As layer/shaders.cpp: the SHaRC headers (this checkout's external/, else the main checkout's)
# and a first line "// dxc: <arguments>".
sharc = []
for c in (root / "external" / "sharc-1.8.3" / "include", root.parents[2] / "external" / "sharc-1.8.3" / "include"):
    if (c / "SharcCommon.h").exists():
        sharc = ["-I", str(c), "-D", "BG3RTX_SHARC_SDK=1"]
        break
rc = 0
with tempfile.TemporaryDirectory() as tmp:
    for f in args:
        spv = Path(tmp) / (Path(f).stem + ".spv")
        first = (shaders / f).read_text(errors="replace").split("\n", 1)[0].strip()
        extra = first[len("// dxc:"):].split() if first.startswith("// dxc:") else []
        r = subprocess.run([str(sdk / "dxc.exe"), str(shaders / f), "-spirv", "-fspv-target-env=vulkan1.3", "-HV", "2021",
                            "-T", "cs_6_6", "-E", "main", "-I", str(shaders), "-D", "BG3RTX=1", "-O3"] + sharc + extra +
                           ["-Fo", str(spv)], capture_output=True, text=True)
        if r.returncode:
            print(f"{f}: compile failed\n{r.stdout}{r.stderr}")
            rc = 1
            continue
        refl = json.loads(subprocess.run([str(sdk / "spirv-cross.exe"), str(spv), "--reflect"], capture_output=True,
                                         text=True, check=True).stdout)
        lines = []
        for k, t in kind.items():
            for v in refl.get(k, []):
                lines.append(f"{v['set']} {v['binding']} {t} {(v.get('array') or [1])[0]}")
        lay = Path(tmp) / (Path(f).stem + ".txt")
        lay.write_text("\n".join(lines) + "\n")
        r = subprocess.run([str(root / "bin" / "pipeline_stats.exe"), str(spv), str(lay)], capture_output=True, text=True)
        print(f"== {f}\n{r.stdout}{r.stderr}", end="")
        rc |= r.returncode
sys.exit(rc)
