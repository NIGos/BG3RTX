"""Water surface arithmetic OFF identity and capability SPIR-V validation.

The baseline receives only the exact, fixture-tested F03 half-range clamp and F06
mip-coordinate ordering corrections before comparison; unknown forms fail closed.
The compile-time REFERENCE=0 form proves dormant arithmetic matches that baseline
for every compose request. This is separate from raw ordinary request identity:
capability modules gain a specialization branch and therefore change raw bytes.
Raw differences must pass selective retired-feature equivalence for that same
request/reference, with only removed SpecIds 1001/1002/1005/1006/1016-1019/1021-1024 fixed at zero.
The separate C2 default comparison fixes 1026 on both sides; other live IDs remain variable.
The production module uses specialization 1025; no reference define is requested
at runtime. All compilation uses the serial cached BelowNormal shared helper.
"""
import argparse
import json
from pathlib import Path
import sys
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.shader_identity import SourceTree, compile_requests, default_baseline, gate_setup, git_blob_id, identical, identity, split_request, unchanged_since  # noqa: E402
from tools.hair_retirement_check import retirement_fallback  # noqa: E402


# Correct only these independently fixture-tested defects in the old source.
# All other historical water/alpha code still goes through the identity proof.
_WATER_OLD = b"  wRad[fp] = float4(all(isfinite(L)) ? max(L, 0.0) : 0.0.xxx, float(cls));"
_WATER_FIXED = _WATER_OLD.replace(b"max(L, 0.0)", b"clamp(L, 0.0, 65504.0)")
_MIP_OLD = b"""  float2 ip = floor(u * dims * pow(0.5, mip));
  // Stay inside the indirection texture (constants from another tile set, or a
  // mip it does not have, would fetch out of bounds).
  uint iw, ih, ilevels;
  gAlphaTex[NonUniformResourceIndex(tex)].GetDimensions(0, iw, ih, ilevels);
  mip = min(mip, float(max(ilevels, 1u) - 1u));"""
_MIP_FIXED = _MIP_OLD.split(b"\n", 1)[1] + b"\n" + _MIP_OLD.split(b"\n", 1)[0]
_BASELINE_CORRECTIONS = (("F03", "shaders/rt/water_compose.hlsl", _WATER_OLD, _WATER_FIXED),
                         ("F06", "shaders/rt/rt_common.hlsli", _MIP_OLD, _MIP_FIXED))


def corrected_baseline(base):
    """Exact source overlay, never a working-tree replacement or SPIR-V waiver."""
    replacements = {}
    for finding, path, before, after in _BASELINE_CORRECTIONS:
        data = base.read(path)
        if b"\r\n" in data:
            before, after = (part.replace(b"\n", b"\r\n") for part in (before, after))
        if data.count(before) + data.count(after) != 1:
            raise ValueError(f"{finding}: unrecognized or duplicated baseline correction in {path}")
        replacements[path] = data.replace(before, after)
    ids = dict(base.ids)
    ids.update({path: git_blob_id(data) for path, data in replacements.items()})
    tree = SourceTree(base.label + " + exact F03/F06 corrections", ids,
                      lambda path: replacements[path] if path in replacements else base.read(path))
    tree.sharc_dir = base.sharc_dir
    return tree


class BaselineCorrectionTest(unittest.TestCase):
    def baseline(self, fixed=False):
        files = {path: b"// retained prefix\n" + (after if fixed else before) + b"\n// retained suffix\n"
                 for _, path, before, after in _BASELINE_CORRECTIONS}
        files["shaders/unrelated.hlsl"] = b"unrelated code must remain byte-identical"
        return SourceTree.memory(files)

    def test_exact_changes_and_unrelated_bytes(self):
        base = self.baseline()
        fixed = corrected_baseline(base)
        for _, path, before, after in _BASELINE_CORRECTIONS:
            self.assertEqual(fixed.read(path), base.read(path).replace(before, after))
            self.assertEqual(fixed.ids[path], git_blob_id(fixed.read(path)))
        path = "shaders/unrelated.hlsl"
        self.assertEqual(fixed.read(path), base.read(path))
        self.assertEqual(fixed.ids[path], base.ids[path])
        self.assertIsNone(fixed.disk)  # compilation materializes the patched bytes

    def test_already_fixed_is_idempotent(self):
        base = self.baseline(fixed=True)
        self.assertEqual(corrected_baseline(base).ids, base.ids)

    def test_unknown_and_duplicate_code_fail_closed(self):
        for _, path, before, _ in _BASELINE_CORRECTIONS:
            for replacement in (b"unknown code", before + b"\n" + before):
                base = self.baseline()
                base._data[path] = replacement
                with self.assertRaises(ValueError):
                    corrected_baseline(base)

    def test_crlf_baseline(self):
        base = self.baseline()
        for path in base.ids:
            base._data[path] = base.read(path).replace(b"\n", b"\r\n")
        fixed = corrected_baseline(base)
        for _, path, _, after in _BASELINE_CORRECTIONS:
            self.assertIn(after.replace(b"\n", b"\r\n"), fixed.read(path))


def reference_off(request):
    return request + (";" if "?" in request else "?") + "WATER_SURFACE_FRESNEL_REFERENCE=0"


def counter_contract(blob):
    """Inspect the emitted production module: all 12 reason lanes have atomic sinks.

    This catches an omitted reason call, a shortened shared array and compile-time
    elimination of diagnostics when specialization 1025 defaults to zero.
    """
    words = struct.unpack(f"<{len(blob)//4}I", blob)
    instructions = []
    at = 5
    while at < len(words):
        size, opcode = words[at] >> 16, words[at] & 65535
        if not size or at + size > len(words):
            return "malformed SPIR-V"
        instructions.append((opcode, words[at+1:at+size]))
        at += size
    constants = {a[1]: a[2] for op,a in instructions if op == 43 and len(a) == 3}
    names = {a[0]: struct.pack(f"<{len(a)-1}I", *a[1:]).split(b"\0")[0].decode()
             for op,a in instructions if op == 5}
    shared = next((i for i,n in names.items() if n == "gsSurface"), None)
    if shared is None:
        return "missing gsSurface"
    array_lengths = {a[0]: constants.get(a[2]) for op,a in instructions if op == 28}
    pointer_types = {a[0]: (a[1], a[2]) for op,a in instructions if op == 32}
    variable = next((a for op,a in instructions if op == 59 and a[1] == shared), None)
    pointee = pointer_types.get(variable[0], ()) if variable else ()
    if not pointee or pointee[0] != 4 or array_lengths.get(pointee[1]) != 12:
        return "gsSurface is not a 12-word workgroup array"
    accesses = {a[1]: constants.get(a[-1]) for op,a in instructions if op in (65,66) and a[2] == shared}
    atomic_lanes = {accesses[a[2]] for op,a in instructions if op == 234 and a[2] in accesses}
    if not set(range(12)) <= atomic_lanes:
        return f"missing reason atomic lanes {sorted(set(range(12))-atomic_lanes)}"
    # Decode clean/reason bits, rather than merely carrying unused integer literals.
    bit_masks = {constants[x] for op,a in instructions if op == 199 for x in a[2:] if x in constants}
    if not {1,8,16,32,255,49152,49160} <= bit_masks:
        return f"missing marker/reason decoding masks {sorted({1,8,16,32,255,49152,49160}-bit_masks)}"
    if not any(op == 71 and len(a) == 3 and a[1:] == (1,1025) for op,a in instructions):
        return "missing specialization 1025"
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", default=default_baseline())
    parser.add_argument("--dxc")
    parser.add_argument("--sharc", default="auto")
    parser.add_argument("--spirv-val")
    parser.add_argument("--json")
    parser.add_argument("--test-baseline-overlay", action="store_true",
                        help="run the source-overlay fixtures without compiling shaders")
    args = parser.parse_args()
    fixtures = unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(BaselineCorrectionTest))
    if not fixtures.wasSuccessful() or args.test_baseline_overlay:
        return int(not fixtures.wasSuccessful())
    tool, cache, base, new = gate_setup(args.baseline, args.dxc, args.sharc, args.spirv_val)
    historical_base = base
    base = corrected_baseline(historical_base)
    old = [r for r in base.requests() if split_request(r)[0] == "rt/water_compose.hlsl"
           and "WATER_HIT_SHADE_LAYOUT=1" not in split_request(r)[1]]
    current = set(new.requests())
    missing = [r for r in old if r not in current]
    requests = [r for r in old if r in current]
    off = [reference_off(r) for r in requests]
    ordinary = [r for r in requests if "WATER_SPEC_RF_EDGE=1" not in split_request(r)[1]]
    try:
        results = identity(base, new, off, tool, cache, 1, "water surface arithmetic OFF")
        raw = identity(base, new, ordinary, tool, cache, 1, "water surface raw ordinary")
        off_proofs = retirement_fallback(results, base, new, tool, cache, "arithmetic OFF")
        raw_proofs = retirement_fallback(raw, base, new, tool, cache, "raw ordinary")
        # Default specialization branches are retained and checked by spirv-val;
        # these are the actual runtime requests, not the arithmetic reference.
        on = compile_requests(new, requests, tool=tool, cache=cache, jobs=1,
                              label="water surface capability", validate=True)
        unchanged_since(new)
    finally:
        base.cleanup()
        historical_base.cleanup()
        new.cleanup()
    failed_off = [p for p in off_proofs if p['path'] == 'failed']
    failed_on = [r for r in on.values() if not r.ok or not r.valid]
    counter_failures = []
    for request, result in on.items():
        if result.ok and "WATER_SPEC_RF_EDGE=1" in split_request(request)[1]:
            failure = counter_contract(result.blob("water_compose"))
            if failure:
                counter_failures.append((request, failure))
    for request, failure in counter_failures:
        print(f"FAIL counter contract {request}: {failure}")
    failed_raw = [p for p in raw_proofs if p['path'] == 'failed']
    for p in failed_off:
        print(f"FAIL arithmetic OFF {p['request']}: {p['raw_state']}\n{p['error']}")
    for p in failed_raw:
        print(f"FAIL raw ordinary {p['request']}: {p['raw_state']}\n{p['error']}")
    for r in failed_on:
        print(f"FAIL capability {r.request}: {r.log}")
    report = {"baseline": args.baseline, "compose_requests": len(requests),
              "baseline_source_corrections": ["F03: clamp wRad to finite half range", "F06: clamp mip before indirection coordinates"],
              "missing": missing, "arithmetic_off_identical": sum(identical(s) for _, s, _ in results),
              "arithmetic_off_accepted": len(results) - len(failed_off),
              "arithmetic_off_paths": off_proofs, "raw_ordinary_paths": raw_proofs,
              "production_validated": len(on) - len(failed_on),
              "raw_ordinary_requests": len(ordinary), "raw_ordinary_identical": sum(identical(s) for _, s, _ in raw),
              "raw_ordinary_accepted": len(raw) - len(failed_raw),
              "counter_contract_failures": counter_failures,
              "raw_capability_identity": "not claimed; capability modules add specialization arithmetic"}
    if args.json:
        Path(args.json).write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return int(not requests or bool(missing or failed_off or failed_on or failed_raw or counter_failures))


if __name__ == "__main__":
    sys.exit(main())
