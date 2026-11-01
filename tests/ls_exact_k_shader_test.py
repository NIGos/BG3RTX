"""ls_k offline shader gate (docs/TRACK_D_LOCAL_LIGHTS.md 9.11, exact-k + RIS remainder).

1. Key off: every shader request of the baseline (shaders/**/*.hlsl + shaders/variants.txt at the
   merge-base with main) compiles to byte-identical SPIR-V in this tree: rt_local_shadows and its
   box/full/check/reference variants and every other pass are untouched (LS_K=0 compiles none of it).
2. Key on: every LS_K request (gi/rt_local_shadows{,_box,_full,_full_box}_k.hlsl with its flags)
   compiles and passes spirv-val.
CPU compiler tools only: no Vulkan device, no game, no queue. Compiles through the shared cache
of tools/shader_identity.py.

    py -3 tests/ls_exact_k_shader_test.py [--baseline <commit>] [--jobs N] [--no-cache]
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.shader_identity import feature_gate  # noqa: E402

FEATURE = ("gi/rt_local_shadows_k.hlsl", "gi/rt_local_shadows_box_k.hlsl", "gi/rt_local_shadows_full_k.hlsl",
           "gi/rt_local_shadows_full_box_k.hlsl")

if __name__ == "__main__":
    sys.exit(feature_gate(__doc__, FEATURE, "ls_k (LS_K)"))
