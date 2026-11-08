"""Post steps of the capture -> conversion -> strands live jobs (docs/RTX_HAIR_ROADMAP.md 2d). Runs in the job's tree.

python tools/live_batches/hair_capture/stage.py convert   (post of hcap2-capture)
    moves the .hcap files the game wrote into <tree>/data/assetdb/hair_capture (the assetdb the layer used: the tree's
    own or the main checkout's) to a scratch assetdb copy: <tree>/data/assetdb is created with a copy of
    bg3rtx_assetdb.bin, hair_capture/ (the moved captures) and hair_grooms/ written by bin/bg3rtx_hairconv (the
    launcher's flow, one style at a time). The real assetdb's hair_grooms is never written; the tree's copy carries a
    marker so cleanup only removes what this script made.
python tools/live_batches/hair_capture/stage.py cleanup   (post of hcap2-strands)
    removes the tree's assetdb copy (marker required) so later jobs use the real assetdb again.
Exit 1 when a step cannot run (post_gate).
"""
import shutil
import subprocess
import sys
from pathlib import Path

TREE = Path.cwd()
COPY = TREE / "data" / "assetdb"
MARKER = COPY / "HAIR_CAPTURE_E2E_COPY"


def real_assetdb():
    """The assetdb folder the layer reads when the tree has none (layer/assetdb.cpp: the main checkout's)."""
    own = TREE / "data" / "assetdb" / "bg3rtx_assetdb.bin"
    if own.exists() and not MARKER.exists():
        return own.parent
    for parent in TREE.parents:
        cand = parent / "data" / "assetdb" / "bg3rtx_assetdb.bin"
        if cand.exists() and (parent / ".git").is_dir():
            return cand.parent
    return None


def linked(p):
    """A junction or symlink (AGENTS.md: never write into or delete through a link to the shared assetdb)."""
    import os
    return p.is_symlink() or os.path.isjunction(p)


def convert():
    if linked(COPY) or linked(COPY.parent):
        print("stage: data/assetdb of the tree is a link to shared data: refusing")
        return 1
    if COPY.exists() and not MARKER.exists():
        print("stage: the tree has its own assetdb (not ours): refusing to stage a copy")
        return 1
    real = real_assetdb()
    if real is None:
        print("stage: no assetdb found")
        return 1
    caps = sorted((real / "hair_capture").glob("*.hcap"))
    if not caps:
        print(f"stage: no captures in {real / 'hair_capture'}")
        return 1
    (COPY / "hair_capture").mkdir(parents=True, exist_ok=True)
    (COPY / "hair_grooms").mkdir(exist_ok=True)
    MARKER.write_text("created by tools/live_batches/hair_capture/stage.py\n", encoding="utf-8")
    shutil.copy2(real / "bg3rtx_assetdb.bin", COPY / "bg3rtx_assetdb.bin")
    for c in caps:   # moved: the real assetdb returns to its state before the run
        shutil.move(str(c), COPY / "hair_capture" / c.name)
    try:
        (real / "hair_capture").rmdir()
    except OSError:
        pass
    exe = TREE / "bin" / "bg3rtx_hairconv.exe"
    r = subprocess.run([str(exe), str(COPY / "hair_capture"), "--out", str(COPY / "hair_grooms"), "--report-dir",
                        str(COPY / "hair_batch"), "--threads", "4", "--jobs", "1"], capture_output=True, text=True)
    print(r.stdout[-2000:], r.stderr[-500:])
    grooms = list((COPY / "hair_grooms").glob("*.hgrm"))
    print(f"stage: {len(caps)} captures -> {len(grooms)} grooms in {COPY / 'hair_grooms'}")
    return 0 if r.returncode == 0 and grooms else 1


def cleanup():
    if not COPY.exists():
        print("stage: nothing to clean")
        return 0
    if linked(COPY) or linked(COPY.parent) or not MARKER.exists():
        print("stage: the tree's assetdb is not ours (or a link): left alone")
        return 1
    shutil.rmtree(COPY)
    print(f"stage: removed {COPY}")
    return 0


if __name__ == "__main__":
    sys.exit({"convert": convert, "cleanup": cleanup}[sys.argv[1]]())
