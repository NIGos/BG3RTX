"""Compile every compute shader offline, as layer/shaders.cpp does at run time (no game).

    py -3 tools/compile_shaders.py [--pp-verify N] [files relative to shaders/ ...]

DXC from the Vulkan SDK, -spirv vulkan1.3, HLSL 2021, cs_6_6, -O3, the SHaRC headers (this
checkout's external/, else the main checkout's) and a first line "// dxc: <arguments>".
A request "<file>?NAME=VALUE;NAME=VALUE" adds each pair as -D (common/shader_variant.h).
Without arguments: every shaders/**/*.hlsl plus every GI variant request in shaders/variants.txt.
Compiles through tools/shader_identity.py: a request whose DXC, flags, source and every
transitively included file are unchanged reuses its cached (re-hashed) SPIR-V; any input change
recompiles it, unless its preprocessed text was compiled before (second key level). --no-cache compiles
everything; --pp-verify N compiles N random requests from scratch and fails on any difference. Progress at least every 15 s. Prunes the
cache at the end (tools/shader_identity.py --prune defaults: 14 days, 3 baselines, 2 GB).
Exit code 1 if any fails.
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from tools.shader_identity import Cache, MemoryCache, SourceTree, Toolchain, compile_requests, lower_priority, prune, verify_pp  # noqa: E402

lower_priority()
root = Path(__file__).resolve().parent.parent
argv = sys.argv[1:]
verify = 0
if "--pp-verify" in argv:   # soundness self-check of the preprocessed key (tools/shader_identity.py verify_pp)
    i = argv.index("--pp-verify")
    verify = int(argv[i + 1])
    del argv[i:i + 2]
args = [a for a in argv if a != "--no-cache"]
tool = Toolchain()
if not tool.dxc.exists():
    print(f"DXC not found: {tool.dxc}")
    sys.exit(1)
tree = SourceTree.worktree(root).with_sharc(tool.sharc)
files = args or tree.requests()
cache = MemoryCache() if "--no-cache" in sys.argv else Cache()


def defines(f):
    """The -D pairs of a request "<file>?A=1;B=2"."""
    return [d for d in f.partition("?")[2].split(";") if d]


compiled = compile_requests(tree, files, tool=tool, cache=cache, label="compile_shaders", backfill=True)
res = [(f, 0 if compiled[f].ok else 1, compiled[f].log) for f in files]


# The checkers take many modules on one command line: short copies of the cached blobs keep it
# under the Windows limit (the cache paths are long).
short = tempfile.TemporaryDirectory(prefix="b3m-", ignore_cleanup_errors=True)
short_names = {}


def spv(f, e="main"):
    src = compiled[f].path(e)
    if src not in short_names:
        short_names[src] = str(Path(short.name) / f"{len(short_names)}.spv")
        shutil.copyfile(src, short_names[src])
    return short_names[src]


# These production GI variants need their dedicated host interface, not
# the contracted local-shadow layout. Check the actual compiled modules.
material_gi = [(spv(f), ("RT_NATIVE_TILESET=1" in defines(f) or "RT_SPEC_NATIVE=1" in defines(f)))
               for f, rc, _ in res if not rc and f.partition("?")[0].endswith(
                   ("gi_trace_rt_hit_direct.hlsl", "gi_trace_rt_sharc_hit_direct.hlsl"))]
# rt_hit_emission: the hit-GI variants plus the emissive set 2 (--em), with and without the sun.
em_gi = [(spv(f), ("RT_NATIVE_TILESET=1" in defines(f) or "RT_SPEC_NATIVE=1" in defines(f)), "_hit_sun_em" in f.partition("?")[0])
         for f, rc, _ in res if not rc and f.partition("?")[0].endswith(
             ("gi_trace_rt_hit_direct_em.hlsl", "gi_trace_rt_sharc_hit_direct_em.hlsl",
              "gi_trace_rt_hit_sun_em.hlsl", "gi_trace_rt_sharc_hit_sun_em.hlsl"))]
# gi_hit_sun: the same contract plus the sun set 3 and the roof volumes b47 (--sun).
sun_gi = [(spv(f), ("RT_NATIVE_TILESET=1" in defines(f) or "RT_SPEC_NATIVE=1" in defines(f)))
          for f, rc, _ in res if not rc and f.partition("?")[0].endswith(
              ("gi_trace_rt_hit_sun.hlsl", "gi_trace_rt_sharc_hit_sun.hlsl"))]
references = [(spv(f), ("RT_NATIVE_TILESET=1" in defines(f) or "RT_SPEC_NATIVE=1" in defines(f)))
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
        args = [*(["--tileset"] if tileset else []), *gi, "--sun", *sgi,
                "--em", *esgi, "--nosun", *egi, "--reference", *refs]
        # A response file (one argument per line, expanded in place by the checker): every module
        # path on the command line exceeds the Windows 32767-character limit (WinError 206).
        with tempfile.TemporaryDirectory() as tmp:
            rsp = Path(tmp) / "gi_bindings_args.txt"
            rsp.write_text("\n".join(str(a) for a in args) + "\n", encoding="utf-8")
            result = subprocess.run([str(checker), f"@{rsp}"], capture_output=True, text=True)
        if result.returncode:
            res.append(("GI host descriptor interface", result.returncode, result.stdout + result.stderr))
        else:
            print(f"GI/reference host descriptor interface checked: {len(gi)} GI + {len(sgi)} GI sun + {len(egi) + len(esgi)} GI emission + {len(refs)} reference SPIR-V modules; native tileset={tileset}")
elif material_gi or references or sun_gi:
    print("SKIP GI/reference host descriptor interface: build native_hit_gi_bindings_test first; shader compilation alone is not interface validation")
# rf_hit_shade (common/native_hit_rf_bindings.h): each compiled tier (b) module against its layout.
rf_hit = [(f, spv(f)) for f, rc, _ in res if not rc and
          f.partition("?")[0] in ("gi/rt_reflections_hit_shade.hlsl", "gi/rt_reflections_sharc_hit_shade.hlsl",
                                  "gi/rt_reflections_hit_shade_em.hlsl", "gi/rt_reflections_sharc_hit_shade_em.hlsl")]
rf_checker = root / "bin" / "native_hit_rf_bindings_test.exe"
if rf_hit and rf_checker.exists():
    for f, module in rf_hit:
        d = defines(f)
        flags = (["--sharc"] if "sharc" in f.partition("?")[0] else []) + \
                (["--tileset"] if ("RT_NATIVE_TILESET=1" in d or "RT_SPEC_NATIVE=1" in d) else []) + \
                (["--hair"] if ("RT_HAIR_PHYSICAL_ORIGIN=1" in d or "RT_SPEC_HAIR_ORIGIN=1" in d) else []) + \
                (["--cascade"] if "RT_CASCADE_MASKS=1" in d else []) + \
                (["--em"] if f.partition("?")[0].endswith("_em.hlsl") else []) + \
                (["--gi"] if ("RF_HIT_GI=1" in d or "RF_SPEC_HIT_GI=1" in d) else [])
        result = subprocess.run([str(rf_checker), *flags, module], capture_output=True, text=True)
        if result.returncode:
            res.append(("rf hit shade descriptor interface " + f, result.returncode, result.stdout + result.stderr))
    if not any(r[1] and r[0].startswith("rf hit shade") for r in res):
        print(f"rf hit shade descriptor interface checked: {len(rf_hit)} SPIR-V modules")
elif rf_hit:
    print("SKIP rf hit shade descriptor interface: build native_hit_rf_bindings_test first")
if verify:
    for r in verify_pp(tree, files, verify, tool, cache):
        res.append(("preprocessed cache key self-check " + r, 1, "cached SPIR-V differs from a fresh compile"))
bad = [r for r in res if r[1]]
for f, rc, msg in bad:
    print(f"FAIL {f}")
    print(msg[:4000])
if not isinstance(cache, MemoryCache):
    try:
        prune(cache, tool=tool)
    except Exception as e:  # noqa: BLE001  pruning never fails the gate
        print(f"shader cache prune skipped: {e!r}")
cached = sum(1 for f in files if compiled[f].cached)
print(f"{len(res) - len(bad)}/{len(res)} compiled ({cached} from the cache)" +
      ("" if tool.sharc else " (no SHaRC SDK found)"))
sys.exit(1 if bad else 0)
