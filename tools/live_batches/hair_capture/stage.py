"""Steps of the capture -> conversion -> strands live jobs (docs/RTX_HAIR_ROADMAP.md 2e). Runs in the job's tree.

The layer reads <tree>/data/assetdb when it holds bg3rtx_assetdb.bin (layer/assetdb.cpp: "this checkout"), else the
main checkout's. These steps build such a scratch copy so the real assetdb is never written:

  stage.py setup <index id>    (command job hcap3-setup, before launch 1)
      <tree>/data/assetdb: hard links of every file of the real assetdb (same volume, no copy; files are replaced, never
      written in place) except hair_grooms/<index id>.hgrm, the close-up companion whose groom is removed so the game
      captures that style with its full-size mask. A marker file names the copy as ours.
  stage.py convert <index id>  (post of hcap3-capture)
      bin/bg3rtx_hairconv over <copy>/hair_capture into <copy>/hair_grooms (the launcher's flow), then the end-to-end
      equivalence: the regenerated groom against the removed original (read-only from the real assetdb): strand and
      point counts, the share of identical strand embeddings, radii ratio, and the converter's verdict / IoU.
      Writes <tree>/captures/hcap3_e2e.json. Exit 1 when no groom for <index id> was written.
  stage.py cleanup             (post of hcap3-strands)
      removes the copy (links only; marker required).
"""
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

TREE = Path.cwd()
COPY = TREE / "data" / "assetdb"
MARKER = COPY / "HAIR_CAPTURE_E2E_COPY"


def linked(p):
    """A junction or symlink (AGENTS.md: never write into or delete through a link to the shared assetdb)."""
    return p.is_symlink() or os.path.isjunction(p)


def real_assetdb():
    for parent in TREE.parents:
        cand = parent / "data" / "assetdb"
        if (cand / "bg3rtx_assetdb.bin").exists() and (parent / ".git").is_dir() and not linked(cand):
            return cand
    return None


def setup(target):
    if linked(COPY) or linked(COPY.parent):
        print("stage: data/assetdb of the tree is a link to shared data: refusing")
        return 1
    if COPY.exists():
        print("stage: the tree already has data/assetdb: refusing (cleanup first)")
        return 1
    real = real_assetdb()
    if real is None or not (real / "hair_grooms" / f"{target}.hgrm").exists():
        print(f"stage: real assetdb or the target groom {target} not found")
        return 1
    COPY.mkdir(parents=True)
    MARKER.write_text(f"created by tools/live_batches/hair_capture/stage.py; removed groom {target}\n", encoding="utf-8")
    n = 0
    for src in real.rglob("*"):
        rel = src.relative_to(real)
        if rel.parts[0] in ("hair_capture",) or rel == Path("hair_grooms") / f"{target}.hgrm":
            continue
        dst = COPY / rel
        if src.is_dir():
            dst.mkdir(parents=True, exist_ok=True)
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        os.link(src, dst)   # same volume: no data copied; the converter replaces files (tmp + rename), never edits
        n += 1
    print(f"stage: {COPY}: {n} files linked from {real}, groom {target} removed (the capture target)")
    return 0


def convert(target):
    if not MARKER.exists() or linked(COPY):
        print("stage: no scratch assetdb copy (setup did not run)")
        return 1
    caps = sorted((COPY / "hair_capture").glob("*.hcap"))
    print(f"stage: {len(caps)} captures: {[c.name for c in caps][:12]}")
    exe = TREE / "bin" / "bg3rtx_hairconv.exe"
    out = TREE / "captures"
    out.mkdir(exist_ok=True)
    r = subprocess.run([str(exe), str(COPY / "hair_capture"), "--out", str(COPY / "hair_grooms"), "--report-dir",
                        str(COPY / "hair_batch"), "--threads", "4", "--jobs", "1", "--json", str(out / "hcap3_hairconv.json")],
                       capture_output=True, text=True)
    print(r.stdout[-1500:], r.stderr[-300:])
    new = COPY / "hair_grooms" / f"{target}.hgrm"
    summary = {"captures": [c.name for c in caps], "target": target, "written": new.exists()}
    try:
        summary["converter"] = [x for x in json.loads((out / "hcap3_hairconv.json").read_text()) if target in x["name"]]
    except (OSError, ValueError):
        pass
    if new.exists():
        sys.path.insert(0, str(TREE))
        import numpy as np
        from tools.hair_groom import storage
        a, b = storage.read_any(new), storage.read_any(real_assetdb() / "hair_grooms" / f"{target}.hgrm")
        la, lb = a.lods[0], b.lods[0]
        oa, ob = storage.strand_offsets(la.points, la.segment), storage.strand_offsets(lb.points, lb.segment)
        key = lambda l, o: {(int(l.triangle[s]), tuple(np.round(l.bary[s], 5))) for s in o[:-1]}
        ka, kb = key(la, oa), key(lb, ob)
        summary.update(index_id_equal=a.header[16:24] == b.header[16:24], strands=[len(oa) - 1, len(ob) - 1],
                       points=[int(la.points), int(lb.points)], shared_strand_starts=len(ka & kb) / max(len(kb), 1),
                       radius_median_ratio=float(np.median(la.radius) / np.median(lb.radius)))
    (out / "hcap3_e2e.json").write_text(json.dumps(summary, indent=1), encoding="utf-8")
    print("stage: e2e", json.dumps({k: summary.get(k) for k in ("written", "index_id_equal", "strands", "points",
                                                                    "shared_strand_starts", "radius_median_ratio")}))
    return 0 if new.exists() else 1


def cleanup():
    if not COPY.exists():
        print("stage: nothing to clean")
        return 0
    if linked(COPY) or linked(COPY.parent) or not MARKER.exists():
        print("stage: the tree's assetdb is not ours (or a link): left alone")
        return 1
    shutil.rmtree(COPY)   # hard links: removes the names, the real assetdb's files stay
    print(f"stage: removed {COPY}")
    return 0


if __name__ == "__main__":
    cmd = sys.argv[1]
    sys.exit(setup(sys.argv[2]) if cmd == "setup" else convert(sys.argv[2]) if cmd == "convert" else cleanup())
