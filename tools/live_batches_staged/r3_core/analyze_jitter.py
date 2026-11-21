# r3-d3-jitter analysis (2026-11-21, docs/progress/claude-r3-core-live.md): output-grade luma per K8 burst, masks from
# the OFF windows' A. Its raw bursts were deleted after the run; results in RESULT_20261121.json.
import json, sys, numpy as np
from pathlib import Path
C = Path(r"E:\Storage\Code Projects\BG3RTX\.claude\worktrees\test-main\captures")
B = {"off-a": "pan-20261121-034201-98900", "on": "pan-20261121-034232-98901", "off-b": "pan-20261121-034302-98902"}
def load(d):
    meta = json.loads((C / d / "burst.json").read_text())
    fr = {}
    for s in meta["samples"]:
        a = np.fromfile(C / d / s["file"], dtype=np.uint8)
        w, h = s["width"], s["height"]
        if s["format"] == 64:
            u = a.view(np.uint32).reshape(h, w)
            rgb = np.stack([(u >> (10 * i)) & 1023 for i in range(3)], -1) / 1023.0
            v = rgb
        elif s["format"] == 97:
            v = a.view(np.float16).reshape(h, w, 4).astype(np.float32)
        else:
            continue
        fr.setdefault(s["record_frame"], {})[s["name"]] = v
    return [fr[k] for k in sorted(fr) if "final" in fr[k] and "rr_post_ac" in fr[k]]
lum = lambda v: v[..., 0] * .2126 + v[..., 1] * .7152 + v[..., 2] * .0722
data = {k: load(v) for k, v in B.items()}
for k, v in data.items(): print(k, "frames", len(v))
H, W = data["off-a"][0]["final"].shape[:2]
h, w = data["off-a"][0]["rr_post_ac"].shape[:2]
yy = (np.arange(H)[:, None] * h // H).repeat(W, 1); xx = (np.arange(W)[None, :] * w // W).repeat(H, 0)
# frozen masks from all OFF frames: mean A at render res, mapped to output pixels
Aoff = np.mean([f["rr_post_ac"][..., 3] for k in ("off-a", "off-b") for f in data[k]], 0)
Amax = np.max([f["rr_post_ac"][..., 3] for k in ("off-a", "off-b") for f in data[k]], 0)
Amin = np.min([f["rr_post_ac"][..., 3] for k in ("off-a", "off-b") for f in data[k]], 0)
A = Aoff[yy, xx]
masks = {"dof_transition (A 0.05-0.95)": (A > .05) & (A < .95),
         "dof_edge (A range over OFF frames > 0.1)": ((Amax - Amin) > .1)[yy, xx],
         "blurred (A < 0.95)": A < .95,
         "in_focus control (A > 0.999)": A > .999}
res = {}
for name, m in masks.items():
    row = {"pixels": int(m.sum())}
    for k, v in data.items():
        L = np.stack([lum(f["final"]) for f in v])
        sd = L.std(0)
        # frame-to-frame |difference| too (jitter shimmer is frame-to-frame)
        dd = np.abs(np.diff(L, axis=0)).mean(0)
        row[k] = {"std_mean": float(sd[m].mean()), "absdiff_mean": float(dd[m].mean()), "mean": float(L.mean(0)[m].mean())}
    res[name] = row
full = {k: float(np.mean([lum(f["final"]).mean() for f in v])) for k, v in data.items()}
res["final_full_image_mean"] = full
print(json.dumps(res, indent=1))
json.dump(res, open(sys.argv[1], "w"), indent=1)
