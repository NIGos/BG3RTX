"""CPU-only DXC validation: all opt-in scalp wrappers and ordinary SPIR-V identity.

Each scalp request (RT_NATIVE_SCALP=1;RT_NATIVE_HAIR=1, alone or with the cascade masks): the same
request without the scalp defines is byte-identical to the baseline, and the scalp request compiles
and passes spirv-val. Compiles through tools/shader_identity.py (shared cache).

Usage: py -3 tests/native_scalp_shader_test.py --dxc PATH --sharc PATH
Result: build-native/shader-check/result.json. No Vulkan/device calls.
"""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.shader_identity import compile_requests, default_baseline, default_jobs, gate_setup, identical, identity  # noqa: E402

SCALP = ["RT_NATIVE_SCALP=1", "RT_NATIVE_HAIR=1"]


def ordinary(request):
    file, _, defs = request.partition("?")
    rest = [d for d in defs.split(";") if d not in SCALP]
    return file + ("?" + ";".join(rest) if rest else "")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dxc", required=True)
    parser.add_argument("--sharc", required=True)
    parser.add_argument("--spirv-val", help="Defaults to spirv-val beside DXC")
    parser.add_argument("--baseline", default=default_baseline(), help="Commit of the key-off reference (default: merge-base with main)")
    parser.add_argument("--jobs", type=int, default=default_jobs())
    parser.add_argument("--no-cache", action="store_true")
    args = parser.parse_args()
    out = ROOT / "build-native/shader-check"
    out.mkdir(parents=True, exist_ok=True)
    tool, cache, base, new = gate_setup(args.baseline, args.dxc, args.sharc, args.spirv_val, args.no_cache)
    # The old shaders/native_scalp/ wrappers: the scalp alone or with the cascade masks.
    wrappers = [l for l in new.requests()
                if "?" in l and all(d in SCALP or d == "RT_CASCADE_MASKS=1" or (d == "RT_CASCADE_SUN=1" and l.startswith("gi/rt_shadow.hlsl?"))
                                    for d in l.partition("?")[2].split(";")) and SCALP[0] in l]
    plain = sorted({ordinary(w) for w in wrappers})
    try:
        same = {r: (s, log) for r, s, log in identity(base, new, plain, tool, cache, args.jobs, "scalp key-off")}
        on = compile_requests(new, wrappers, tool=tool, cache=cache, jobs=args.jobs, label="scalp key-on", validate=True)
        cur = compile_requests(new, plain, tool=tool, cache=cache, jobs=args.jobs, label="scalp ordinary")
    finally:
        base.cleanup()
    records = []
    for w in wrappers:
        state, log = same[ordinary(w)]
        if not identical(state):
            raise RuntimeError(f"Ordinary SPIR-V changed ({state}): {ordinary(w)}\n{log}")
        if not on[w].ok or not on[w].valid:
            raise RuntimeError(f"{w}: {on[w].log}")
        records.append({"entry": ordinary(w), "ordinary_sha256": cur[ordinary(w)].entries[0].sha,
                        "capable_sha256": on[w].entries[0].sha})
    result = {"baseline": args.baseline, "ordinary_identical": len(records), "capable_compiled": len(records),
              "capable_validated": len(records), "entries": records}
    (out / "result.json").write_text(json.dumps(result, indent=2))
    print(f"{len(records)} ordinary byte-identical; {len(records)} capable variants compile")


if __name__ == "__main__":
    main()
