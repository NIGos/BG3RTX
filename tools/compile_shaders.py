"""Compile every compute shader offline, as layer/shaders.cpp does at run time (no game).

    py -3 tools/compile_shaders.py [files relative to shaders/ ...]

DXC from the Vulkan SDK, -spirv vulkan1.3, HLSL 2021, cs_6_6, -O3, the SHaRC headers (this
checkout's external/, else the main checkout's) and a first line "// dxc: <arguments>".
A request "<file>?NAME=VALUE;NAME=VALUE" adds each pair as -D (common/shader_variant.h).
Without arguments: every shaders/**/*.hlsl plus every GI variant request in shaders/variants.txt.
Exit code 1 if any fails.
"""
import hashlib
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
files = sys.argv[1:] or (sorted(str(p.relative_to(shaders)).replace("\\", "/") for p in shaders.rglob("*.hlsl")) +
                         [l.strip() for l in (shaders / "variants.txt").read_text().splitlines()
                          if l.strip() and not l.startswith("#")])


def defines(f):
    """The -D pairs of a request "<file>?A=1;B=2"."""
    return [d for d in f.partition("?")[2].split(";") if d]


def spv_name(f, e):
    # Short: a request with defines would pass MAX_PATH spelled out.
    return f.partition("?")[0].replace("/", "_") + "." + hashlib.sha1(f.encode()).hexdigest()[:12] + f".{e}.spv"


def build(f, tmp):
    src = shaders / f.partition("?")[0]
    text = src.read_text(errors="replace")
    first = text.split("\n", 1)[0].strip()
    extra = first[len("// dxc:"):].split() if first.startswith("// dxc:") else []
    # Every [numthreads] entry (rr_post.hlsl has several), else main.
    entries = re.findall(r"\[numthreads\([^)]*\)\]\s*void\s+(\w+)", text) or ["main"]
    rc, msgs = 0, []
    for e in entries:
        out = Path(tmp) / spv_name(f, e)
        r = subprocess.run([str(dxc), str(src), "-spirv", "-fspv-target-env=vulkan1.3", "-HV", "2021", "-T", "cs_6_6",
                            "-E", e, "-I", str(shaders), "-D", "BG3RTX=1", "-O3"] + sharc +
                           [a for d in defines(f) for a in ("-D", d)] + extra + ["-Fo", str(out)],
                           capture_output=True, text=True)
        rc |= r.returncode
        if r.returncode:
            msgs.append(f"[{e}] " + (r.stdout + r.stderr).strip())
    return f, rc, "\n".join(msgs)


with tempfile.TemporaryDirectory() as tmp, ThreadPoolExecutor(8) as pool:
    res = list(pool.map(lambda f: build(f, tmp), files))
    # These production GI variants need their dedicated host interface, not
    # the contracted local-shadow layout. Check the actual compiled modules.
    material_gi = [(str(Path(tmp) / spv_name(f, "main")), "RT_NATIVE_TILESET=1" in defines(f))
                   for f, rc, _ in res if not rc and f.partition("?")[0].endswith(
                       ("gi_trace_rt_hit_direct.hlsl", "gi_trace_rt_sharc_hit_direct.hlsl"))]
    # rt_hit_emission: the hit-GI variants plus the emissive set 2 (--em), with and without the sun.
    em_gi = [(str(Path(tmp) / spv_name(f, "main")), "RT_NATIVE_TILESET=1" in defines(f), "_hit_sun_em" in f.partition("?")[0])
             for f, rc, _ in res if not rc and f.partition("?")[0].endswith(
                 ("gi_trace_rt_hit_direct_em.hlsl", "gi_trace_rt_sharc_hit_direct_em.hlsl",
                  "gi_trace_rt_hit_sun_em.hlsl", "gi_trace_rt_sharc_hit_sun_em.hlsl"))]
    # gi_hit_sun: the same contract plus the sun set 3 and the roof volumes b47 (--sun).
    sun_gi = [(str(Path(tmp) / spv_name(f, "main")), "RT_NATIVE_TILESET=1" in defines(f))
              for f, rc, _ in res if not rc and f.partition("?")[0].endswith(
                  ("gi_trace_rt_hit_sun.hlsl", "gi_trace_rt_sharc_hit_sun.hlsl"))]
    references = [(str(Path(tmp) / spv_name(f, "main")), "RT_NATIVE_TILESET=1" in defines(f))
                  for f, rc, _ in res if not rc and f.partition("?")[0].rsplit("/", 1)[-1] in
                      ("rt_hit_direct.hlsl", "rt_hit_two_bounce.hlsl")]
    checker = root / "bin" / "native_hit_gi_bindings_test.exe"
    if (material_gi or references or sun_gi) and checker.exists():
        for tileset in (False, True):
            gi = [p for p, t in material_gi if t == tileset]
            sgi = [p for p, t in sun_gi if t == tileset]
            refs = [p for p, t in references if t == tileset]
            if not gi and not refs and not sgi:
                continue
            egi = [p for p, t, s in em_gi if t == tileset and not s]
            esgi = [p for p, t, s in em_gi if t == tileset and s]
            result = subprocess.run([str(checker), *(["--tileset"] if tileset else []), *gi, "--sun", *sgi,
                                     "--em", *esgi, "--nosun", *egi, "--reference", *refs], capture_output=True, text=True)
            if result.returncode:
                res.append(("GI host descriptor interface", result.returncode, result.stdout + result.stderr))
            else:
                print(f"GI/reference host descriptor interface checked: {len(gi)} GI + {len(sgi)} GI sun + {len(egi) + len(esgi)} GI emission + {len(refs)} reference SPIR-V modules; native tileset={tileset}")
    elif material_gi or references or sun_gi:
        print("SKIP GI/reference host descriptor interface: build native_hit_gi_bindings_test first; shader compilation alone is not interface validation")
    # rf_hit_shade (common/native_hit_rf_bindings.h): each compiled tier (b) module against its layout.
    rf_hit = [(f, str(Path(tmp) / spv_name(f, "main"))) for f, rc, _ in res if not rc and
              f.partition("?")[0] in ("gi/rt_reflections_hit_shade.hlsl", "gi/rt_reflections_sharc_hit_shade.hlsl",
                                      "gi/rt_reflections_hit_shade_em.hlsl", "gi/rt_reflections_sharc_hit_shade_em.hlsl")]
    rf_checker = root / "bin" / "native_hit_rf_bindings_test.exe"
    if rf_hit and rf_checker.exists():
        for f, spv in rf_hit:
            d = defines(f)
            flags = (["--sharc"] if "sharc" in f.partition("?")[0] else []) + \
                    (["--tileset"] if "RT_NATIVE_TILESET=1" in d else []) + \
                    (["--hair"] if "RT_HAIR_PHYSICAL_ORIGIN=1" in d else []) + \
                    (["--cascade"] if "RT_CASCADE_MASKS=1" in d else []) + \
                    (["--em"] if f.partition("?")[0].endswith("_em.hlsl") else [])
            result = subprocess.run([str(rf_checker), *flags, spv], capture_output=True, text=True)
            if result.returncode:
                res.append(("rf hit shade descriptor interface " + f, result.returncode, result.stdout + result.stderr))
        if not any(r[1] and r[0].startswith("rf hit shade") for r in res):
            print(f"rf hit shade descriptor interface checked: {len(rf_hit)} SPIR-V modules")
    elif rf_hit:
        print("SKIP rf hit shade descriptor interface: build native_hit_rf_bindings_test first")
bad = [r for r in res if r[1]]
for f, rc, msg in res:
    print(f"{'ok  ' if not rc else 'FAIL'} {f}")
    if rc:
        print(msg[:4000])
print(f"{len(res) - len(bad)}/{len(res)} compiled" + ("" if sharc else " (no SHaRC SDK found)"))
sys.exit(1 if bad else 0)
