"""tools/gi_hit_invariant.py: the comparison on synthetic RGBA16F giRaw images (CTest gi_hit_invariant_test)."""
import sys
import tempfile
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools import gi_hit_invariant as g  # noqa: E402

fails = 0


def check(what, ok):
    global fails
    print(("PASS " if ok else "FAIL ") + what)
    fails += not ok


def as_pair(v):
    v16 = np.asarray(v, dtype=np.float16)
    return v16.view(np.uint16), v16.astype(np.float64)


rng = np.random.default_rng(7)
H, W = 16, 24
base = rng.uniform(0.0, 0.05, (H, W, 4))
base[..., 3] = rng.uniform(0.3, 1.0, (H, W))
bb, bv = as_pair(base)

# 1. Identical images: pass, nothing lower, no change.
r = g.compare(bb, bv, bb, bv)
check("identical: alpha equal everywhere, 0 lower, equal means, gate passes",
      r["alpha_equal"] == H * W and r["lower"] == 0 and r["mean_hit"] == r["mean_base"] and not g.verdict(r))

# 2. Additions only (qualified hits): pass, the added luminance is counted.
hit = bv.copy()
hit[2:5, 3:9, :3] += 0.01
hb, hv = as_pair(hit)
r = g.compare(bb, bv, hb, hv)
check("additions: gate passes, added > 0, 0 lower", not g.verdict(r) and r["added"] > 0 and r["lower"] == 0)

# 3. A value one half-ulp lower (rounding of the stored mean) is not "lower".
hit = bv.copy()
hb, hv = as_pair(hit)
hb = hb.copy()
hb[1, 1, 0] = hb[1, 1, 0] - 1   # one half ulp down
hv = hb.view(np.float16).astype(np.float64)
r = g.compare(bb, bv, hb, hv)
check("one half-ulp lower is within the rounding", r["lower"] == 0)

# 4. A real decrease is reported; small ones do not fail the energy gate when the additions dominate.
hit = bv.copy()
hit[..., :3] += 0.002
hit[0, 0, :3] = 0.0
hb, hv = as_pair(hit)
r = g.compare(bb, bv, hb, hv)
check("one zeroed pixel: lower 1, lost > 0, gate passes (mean higher)", r["lower"] == 1 and r["lost"] > 0 and not g.verdict(r))

# 5. Darker on average: the energy gate fails.
hb, hv = as_pair(np.concatenate([bv[..., :3] * 0.9, bv[..., 3:]], axis=2))
r = g.compare(bb, bv, hb, hv)
check("darker mean fails the energy gate", any("energy rule" in f for f in g.verdict(r)))

# 6. Changed occlusion fails, even when the mean colour is higher.
hit = bv.copy()
hit[..., :3] += 0.01
hit[4, 4, 3] = 0.25
hb, hv = as_pair(hit)
r = g.compare(bb, bv, hb, hv)
check("alpha change fails", r["alpha_equal"] == H * W - 1 and any("bit-equal" in f for f in g.verdict(r)))

# 7. Non-finite components fail.
hit = bv.copy()
hit[3, 3, 1] = np.inf
hb, hv = as_pair(hit)
r = g.compare(bb, bv, hb, hv)
check("non-finite fails", any("non-finite" in f for f in g.verdict(r)))

# 8. File round trip and the pack manifest's giRaw size.
with tempfile.TemporaryDirectory() as t:
    d = Path(t)
    (d / "b").mkdir()
    bb.tofile(d / "b" / "giRaw.raw")
    lb, lv = g.load_giraw(d / "b", W, H)
    (d / "manifest.txt").write_text("bg3rtx_replay_pack 1\npass gi_hit_direct\noutput 0 8 97 24 16 s0b8.live\n", encoding="utf-8")
    check("load_giraw round trip and pack_size", np.array_equal(lb, bb) and g.pack_size(d) == (W, H))

print(f"{fails} failed" if fails else "all passed")
sys.exit(1 if fails else 0)
