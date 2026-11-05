"""Release-path SPIR-V equivalence against a historical tree (no game).

    py -3 tools/release_shader_equivalence.py --baseline 1a7e461c --json report.json

Includes every bare shader and manifest request, as shader_identity.py does.
Removed keys are forced off by the baseline release policy. Retained Boolean
constants are crossed exhaustively, including experimental_options=1 choices.
The hidden native tileset constant is fixed off by enforce_release_policy.
Unbounded address constants remain symbolic on both sides, so equality covers
every address without choosing artificial numeric values. This domain contract
is pinned to the reviewed bundle35 settings policy and fails closed if it changes.
By default each entry first attempts universal symbolic equality over retained
constants. If it succeeds, explicit all-zero/all-one specializations additionally
exercise every retained Boolean value. Otherwise all combinations are checked.
--exhaustive always checks every combination even after symbolic equality.
"""
import argparse
import concurrent.futures
from functools import lru_cache
import hashlib
import itertools
import json
import multiprocessing
import os
from pathlib import Path
import re
import struct
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.shader_identity import (Cache, Progress, SourceTree, Toolchain, compile_many,
                                   default_jobs, lower_priority, split_request, unchanged_since)
from tools.spirv_equivalence import canonical, first_difference, specialize

# Registry and release-policy contract of bundle35. Unknown removals fail closed.
# Explicit zero overrides: guards, IP grass and generated pool rows. The others
# are dev_only knobs absent from kOptInKnobs, hence knob choice 0 is enforced.
REMOVED_KEYS = {1001: "gi_temporal_mv_sample", 1002: "gi_prev_radiance_guard",
                1005: "rt_hit_material_ip_grass", 1006: "rt_hit_material_generated",
                1016: "rt_water_hit_shade", 1021: "rt_water_hit_cost",
                1022: "rt_alpha_metadata_cache", 1023: "rt_water_empty_tiles",
                1024: "rt_compact_hit_state"}
# The domain audit above is of bundle35's settings/release policy. A different
# policy needs a fresh audit, rather than silently applying these assumptions.
REVIEWED_POLICY_SHA256 = "6081e6b3682aeba6c2fe4bf3c51ad0d8d15b21823ea4303cc127262e6b4b9761"


def validate_policy(data):
    if hashlib.sha256(data).hexdigest() != REVIEWED_POLICY_SHA256:
        raise RuntimeError("unreviewed baseline release policy: audit specialization domains before updating the policy receipt")


def shader_ids(tree):
    return {int(n) for p in tree.ids if p.startswith("shaders/")
            for n in re.findall(rb"vk::constant_id\((\d+)\)", tree.read(p))}


def registry_ids(tree):
    text = tree.read("common/shader_variant.h").decode()
    enum = text.split("enum SpecId : unsigned {", 1)[1].split("};", 1)[0]
    return {int(x) for x in re.findall(r"^\s*kSpec\w*\s*=\s*(\d+)", enum, re.M)} | {1000}


@lru_cache(maxsize=16)
def live_ids(blob):
    """Read OpDecorate SpecId directly; no disassembler needed for domain discovery."""
    words = struct.unpack("<" + "I" * (len(blob) // 4), blob)
    out, i = set(), 5
    while i < len(words):
        count, op = words[i] >> 16, words[i] & 65535
        if not count:
            raise ValueError("invalid zero-length SPIR-V instruction")
        if op == 71 and count == 4 and words[i + 2] == 1:
            out.add(words[i + 3])
        i += count
    return out


def baseline_request(request, known):
    if request in known:
        return request
    # The cleanup deleted the guard marker, whose old request carried two specs.
    guarded = request + (";" if "?" in request else "?") + "GI_SPEC_GUARDS=1"
    return guarded if guarded in known else request


def assignments(ids, removed, fixed=None):
    # Every other registered retained constant is an on/off uint. Crossing both
    # values is a conservative superset of release preset and opt-in states.
    fixed = fixed or {}
    retained = sorted(ids - removed - fixed.keys())
    domains = [(0,) if i == 1008 else (0, 1) for i in retained]
    return [dict(zip(retained, values)) | {i: 0 for i in sorted(removed)} | fixed
            for values in itertools.product(*domains)]


class Normalizer:
    """Small persistent digest receipts; no giant disassembly or optimized blobs."""
    def __init__(self, tool, directory):
        self.tool, self.directory = tool, directory
        directory.mkdir(parents=True, exist_ok=True)
        helper = Path(__file__).with_name("spirv_equivalence.py").read_bytes()
        optimizer = tool.dxc.with_name("spirv-opt.exe").read_bytes()
        disassembler = tool.dxc.with_name("spirv-dis.exe").read_bytes()
        self.schema = hashlib.sha256(helper + optimizer + disassembler).hexdigest()

    def get(self, blob, assign, temp, name, full=False):
        # Absent constants cannot affect the module, but are still assigned on
        # both sides when present. Their removal from the cache key is exact.
        ids = live_ids(blob)
        effective = {i: v for i, v in assign.items() if i in ids}
        preserve = sorted(ids - set(assign))
        key = hashlib.sha256(blob + json.dumps([self.schema, sorted(effective.items()), preserve]).encode()).hexdigest()
        receipt = self.directory / (key + ".txt")
        if receipt.exists() and not full:
            return receipt.read_text().strip()
        src, dst = temp / (name + ".spv"), temp / (name + "-opt.spv")
        src.write_bytes(blob)
        try:
            result = canonical(self.tool, specialize(self.tool, src, dst, effective, preserve=preserve))
            digest = hashlib.sha256(json.dumps(result, separators=(",", ":")).encode()).hexdigest()
            pending = receipt.with_name(f"{key}.{os.getpid()}.{threading.get_ident()}.tmp")
            pending.write_text(digest)
            pending.replace(receipt)
            return result if full else digest
        finally:
            src.unlink(missing_ok=True)
            dst.unlink(missing_ok=True)


_WORKER_NORMALIZER = None
_WORKER_PROGRESS = None


def initialize_worker(normalizer, progress_queue):
    global _WORKER_NORMALIZER, _WORKER_PROGRESS
    lower_priority()
    _WORKER_NORMALIZER, _WORKER_PROGRESS = normalizer, progress_queue


def certificate_values(values, universal, exhaustive=False):
    """Only universal symbolic equality permits replacing the Cartesian checks."""
    if not universal or exhaustive or len(values) <= 1:
        return values
    return [values[0], values[-1]]


def check_request(item):
    """One process per active request avoids the Python canonicalizer's GIL.

    Receipts remain shared and content addressed; individual assignments report
    completion to the parent immediately, even for a long request.
    """
    r, entries, count, symbolic = item[:4]
    exhaustive = item[4] if len(item) > 4 else False
    normalizer = _WORKER_NORMALIZER
    first = ""
    certificates = []
    actual_checks = 0
    actual_frozen_checks = 0
    with tempfile.TemporaryDirectory(prefix="b3eq-") as tmp:
        temp = Path(tmp)
        for entry, a, b, values in entries:
            if not values:
                continue
            # Leave retained constants symbolic. Canonical equality is then
            # universal, not evidence inferred from a few sampled assignments.
            universal_assign = {i: 0 for i in REMOVED_KEYS} | {1008: 0} | (item[5] if len(item) > 5 else {})
            universal = (normalizer.get(a, universal_assign, temp, "old") ==
                         normalizer.get(b, universal_assign, temp, "new"))
            actual_checks += 1
            selected = certificate_values(values, universal, exhaustive)
            certificates.append({"entry": entry, "universal_certificate": universal,
                                 "cartesian_assignments": len(values), "explicit_checks": len(selected),
                                 "checked_values": {i: sorted({a[i] for a in selected}) for i in values[0]}})
            entry_bad = False
            for assign in selected:
                da = normalizer.get(a, assign, temp, "old")
                db = normalizer.get(b, assign, temp, "new")
                actual_checks += 1
                actual_frozen_checks += 1
                bad = da != db
                entry_bad |= bad
                if bad and not first:
                    ca = normalizer.get(a, assign, temp, "old", full=True)
                    cb = normalizer.get(b, assign, temp, "new", full=True)
                    first = f"{entry} {assign}: {first_difference(ca, cb)}"
                _WORKER_PROGRESS.put(bad)
            # These states are proven by symbolic equality; the row records
            # their coverage separately from the actual normalization checks.
            if universal and len(selected) < len(values):
                for _ in range(len(values) - len(selected)):
                    _WORKER_PROGRESS.put(entry_bad)
    return r, {"state": "DIFFERENT" if first else "equivalent", "assignments": count,
               "symbolic_constants": symbolic,
               "normalized_pair_checks": actual_checks, "certificates": certificates,
               "actual_frozen_checks": actual_frozen_checks,
               **({"detail": first} if first else {})}


def drain_progress(queue, progress):
    for failed in iter(queue.get, None):
        progress.step(failed=failed)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--baseline", required=True)
    ap.add_argument("--jobs", type=int, default=default_jobs())
    ap.add_argument("--filter")
    ap.add_argument("--json")
    ap.add_argument("--freeze-spec", type=int, action="append", default=[], metavar="ID",
                    help="explicit candidate OFF proof: fix a newly added registered Boolean spec ID to 0")
    ap.add_argument("--exhaustive", action="store_true",
                    help="check every Cartesian assignment even after universal symbolic equality")
    args = ap.parse_args(argv)
    lower_priority()
    start = time.monotonic()
    tool, cache = Toolchain(), Cache()
    base = SourceTree.commit(args.baseline).with_sharc(tool.sharc)
    new = SourceTree.worktree(ROOT).with_sharc(tool.sharc)
    try:
        removed = (registry_ids(base) - registry_ids(new)) | (shader_ids(base) - shader_ids(new))
        if not removed <= set(REMOVED_KEYS):
            raise RuntimeError(f"unreviewed removed spec IDs: {sorted(removed)}")
        if removed:
            validate_policy(base.read("common/settings.h"))
        fixed = {i: 0 for i in args.freeze_spec}
        if not fixed.keys() <= registry_ids(new) - registry_ids(base):
            raise RuntimeError("--freeze-spec must name newly added registered constants only")
        print(f"explicit candidate OFF constants: {fixed}", flush=True)
        print("removed spec constants (release value 0): " + ", ".join(f"{i} {REMOVED_KEYS[i]}" for i in sorted(removed)), flush=True)
        listed = new.requests()
        reqs = [r for r in dict.fromkeys(listed) if not args.filter or re.search(args.filter, r)]
        print(f"request inventory: {len(reqs)} unique selected requests "
              f"({len(listed)} entries before deduplication)", flush=True)
        refs = {r: baseline_request(r, set(base.requests())) for r in reqs}
        cache.note_baseline(base.rev)
        compiled = compile_many([(base, refs[r]) for r in reqs] + [(new, r) for r in reqs],
                                tool=tool, cache=cache, jobs=args.jobs, label="release equivalence compile")
        normalizer = Normalizer(tool, cache.dir / "release_equivalence")
        comparisons = []
        rows = {}
        for r in reqs:
            old, current = compiled[(base.label, refs[r])], compiled[(new.label, r)]
            if not old.ok or not current.ok:
                rows[r] = {"state": "DIFFERENT", "detail": old.log + current.log}
                continue
            if old.digest == current.digest:
                rows[r] = {"state": "identical", "assignments": 0}
                continue
            if [e.name for e in old.entries] != [e.name for e in current.entries]:
                rows[r] = {"state": "DIFFERENT", "detail": "entry point set changed"}
                continue
            entries = []
            count = 0
            symbolic = set()
            for e in old.entries:
                a, b = old.blob(e.name), current.blob(e.name)
                ids = live_ids(a) | live_ids(b)
                unknown = ids - registry_ids(base) - registry_ids(new)
                symbolic.update(unknown)
                values = assignments(ids - unknown, removed, fixed) if a != b else []
                entries.append((e.name, a, b, values))
                count += len(values)
            comparisons.append((r, entries, count, sorted(symbolic), args.exhaustive, fixed))
        total = sum(item[2] for item in comparisons)
        print(f"{len(reqs)} requests, {total} specialized entry comparisons", flush=True)
        with Progress("release equivalence", total, every=30, verb="covered") as progress:
            context = multiprocessing.get_context("spawn")
            queue = context.Queue()
            reader = threading.Thread(target=drain_progress, args=(queue, progress), daemon=True)
            reader.start()
            try:
                with concurrent.futures.ProcessPoolExecutor(args.jobs, mp_context=context,
                        initializer=initialize_worker, initargs=(normalizer, queue)) as pool:
                    futures = [pool.submit(check_request, item) for item in comparisons]
                    for future in concurrent.futures.as_completed(futures):
                        r, row = future.result()
                        rows[r] = row
                        print(f"{row['state']}: {r}" + ("\n" + row["detail"] if "detail" in row else ""), flush=True)
            finally:
                queue.put(None)
                reader.join()
                queue.close()
                queue.join_thread()
        # Negative control: a real retained cascade branch must survive comparison.
        control = next((compiled[(base.label, refs[r])].blob(e.name)
                        for r in reqs for e in (compiled[(base.label, refs[r])].entries or [])
                        if 1000 in live_ids(compiled[(base.label, refs[r])].blob(e.name))), None)
        if control is None:
            raise RuntimeError("no cascade module for mandatory negative control")
        with tempfile.TemporaryDirectory(prefix="b3eq-control-") as tmp:
            assign = {i: 0 for i in live_ids(control) if i in registry_ids(base)}
            control_ok = (normalizer.get(control, assign, Path(tmp), "off") !=
                          normalizer.get(control, assign | {1000: 1}, Path(tmp), "on"))
        unchanged_since(new)
        counts = {s: sum(row["state"] == s for row in rows.values()) for s in ("identical", "equivalent", "DIFFERENT")}
        for r in reqs:
            if rows[r]["state"] == "identical" or "assignments" not in rows[r]:
                print(f"{rows[r]['state']}: {r}" + ("\n" + rows[r]["detail"] if "detail" in rows[r] else ""), flush=True)
        elapsed = time.monotonic() - start
        actual = sum(row.get("normalized_pair_checks", 0) for row in rows.values())
        frozen = sum(row.get("actual_frozen_checks", 0) for row in rows.values())
        universal_count = sum(c["universal_certificate"] for row in rows.values() for c in row.get("certificates", []))
        symbolic_checks = sum(len(row.get("certificates", [])) for row in rows.values())
        print(f"control: {'PASS (must differ)' if control_ok else 'FAIL (blind comparison)'}", flush=True)
        print(f"release equivalence {args.baseline}: {counts}; {total} assignments covered, "
              f"{frozen} frozen checks + {symbolic_checks} symbolic checks "
              f"({universal_count} universal entry certificates); {elapsed:.1f} s", flush=True)
        if args.json:
            Path(args.json).parent.mkdir(parents=True, exist_ok=True)
            Path(args.json).write_text(json.dumps({"baseline": base.rev, "removed": {i: 0 for i in sorted(removed)}, "fixed": fixed,
                                                  "counts": counts, "control": control_ok, "seconds": elapsed,
                                                  "normalized_pair_checks": actual, "universal_certificates": universal_count,
                                                  "actual_frozen_checks": frozen, "symbolic_checks": symbolic_checks,
                                                  "assignments": total, "requests": rows}, indent=2))
        return int(bool(counts["DIFFERENT"] or not control_ok))
    finally:
        base.cleanup()
        new.cleanup()


if __name__ == "__main__":
    sys.exit(main())
