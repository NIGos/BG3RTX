"""rf_hit_shade offline shader gate (docs/PLAN_M4_REFLECTIONS_GI.md M4.2).

1. Key off: every shader request of the baseline (shaders/**/*.hlsl + shaders/variants.txt at the
   merge-base with main) compiles to byte-identical SPIR-V in this tree. That covers rt_reflections
   and its SHaRC variant, D-LS (bg3_sun.hlsli's set macro) and gi_hit_direct (the shared hit
   shading include): the default path and the existing feature paths are untouched.
2. Key on: every rt_reflections*_hit_shade request compiles and passes spirv-val; with
   bin/native_hit_rf_bindings_test.exe built, each module is checked against its descriptor contract.
CPU compiler tools only: no Vulkan device, no game, no queue. Compiles through the shared cache
of tools/shader_identity.py (unchanged inputs are identical by construction, not recompiled).

    py -3 tests/rf_hit_shade_shader_test.py [--baseline <commit>] [--jobs N] [--no-cache]
"""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.shader_identity import feature_gate, split_request  # noqa: E402

FEATURE = ("gi/rt_reflections_hit_shade.hlsl", "gi/rt_reflections_sharc_hit_shade.hlsl")


def rf_contract(r, paths, em=False):
    """native_hit_rf_bindings_test.exe on the modules of request r ('' ok, None when not built)."""
    checker = ROOT / "bin" / "native_hit_rf_bindings_test.exe"
    if not checker.exists():
        return None
    file, d = split_request(r)
    flags = (["--sharc"] if "sharc" in file else []) + (["--tileset"] if "RT_NATIVE_TILESET=1" in d else []) + \
            (["--hair"] if "RT_HAIR_PHYSICAL_ORIGIN=1" in d else []) + \
            (["--cascade"] if "RT_CASCADE_MASKS=1" in d else []) + (["--em"] if em else []) + \
            (["--gi"] if "RF_HIT_GI=1" in d else [])
    c = subprocess.run([str(checker), *flags, *map(str, paths)], capture_output=True, text=True)
    return "" if c.returncode == 0 else "contract: " + c.stdout + c.stderr


if __name__ == "__main__":
    # rf_hit_gi (RF_HIT_GI=1) requests of every hit-shade file, emission twins included, are new feature requests.
    in_feature = lambda r: split_request(r)[0] in FEATURE or "RF_HIT_GI=1" in split_request(r)[1]  # noqa: E731
    contract = lambda r, paths: rf_contract(r, paths, em=split_request(r)[0].endswith("_em.hlsl"))  # noqa: E731
    sys.exit(feature_gate(__doc__, in_feature, "rf_hit_shade", contract))
