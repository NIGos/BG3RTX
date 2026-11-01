"""rt_hit_emission offline shader gate (docs/PLAN_M4_REFLECTIONS_GI.md M4.4).

1. Key off: every shader request of the baseline (shaders/**/*.hlsl + shaders/variants.txt at the
   merge-base with main) compiles to byte-identical SPIR-V in this tree: gi_trace_rt and its hit-GI
   variants, rt_reflections and its rf_hit_shade variants, D-LS and every other pass are untouched.
2. Key on: every *_em request (the hit-GI and rf_hit_shade variants plus the emissive set) compiles,
   passes spirv-val and, with bin/native_hit_{gi,rf}_bindings_test.exe built, its descriptor contract
   (common/hit_emission_bindings.h, --em).
CPU compiler tools only: no Vulkan device, no game, no queue. Compiles through the shared cache
of tools/shader_identity.py.

    py -3 tests/hit_emission_shader_test.py [--baseline <commit>] [--jobs N] [--no-cache]
"""
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from rf_hit_shade_shader_test import ROOT, rf_contract  # noqa: E402
from tools.shader_identity import feature_gate, split_request  # noqa: E402

GI = ("gi/gi_trace_rt_hit_direct_em.hlsl", "gi/gi_trace_rt_sharc_hit_direct_em.hlsl",
      "gi/gi_trace_rt_hit_sun_em.hlsl", "gi/gi_trace_rt_sharc_hit_sun_em.hlsl")
RF = ("gi/rt_reflections_hit_shade_em.hlsl", "gi/rt_reflections_sharc_hit_shade_em.hlsl")
FEATURE = GI + RF


def contract(r, paths):
    file, d = split_request(r)
    if file not in GI:
        return rf_contract(r, paths, em=True)
    checker = ROOT / "bin" / "native_hit_gi_bindings_test.exe"
    if not checker.exists():
        return None
    flags = (["--tileset"] if "RT_NATIVE_TILESET=1" in d else []) + (["--sun"] if "_hit_sun_em" in file else []) + ["--em"]
    c = subprocess.run([str(checker), *flags, *map(str, paths)], capture_output=True, text=True)
    return "" if c.returncode == 0 else "contract: " + c.stdout + c.stderr


if __name__ == "__main__":
    # rf_hit_gi requests (RF_HIT_GI=1) are tests/rf_hit_shade_shader_test.py's feature, compiled there.
    sys.exit(feature_gate(__doc__, lambda r: split_request(r)[0] in FEATURE and "RF_HIT_GI=1" not in split_request(r)[1],
                          "rt_hit_emission", contract, foreign=lambda r: "RF_HIT_GI=1" in r))
