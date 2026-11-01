"""Frame evidence: per VS/FS pair, the draws, passes and scenes of every existing trace, plus
the vertices of the declined pairs from the layer logs' `[hit material families]` lines.

Traces: the captures `scripts.qa.coverage` finds (main checkout and its worktrees), plus any
~/.codex/worktrees/*/BG3RTX/captures/2026*. Read-only. The scene comes from the capture's
run.json only (captures/queue is never read). A draw event's `n` is the number of identical
consecutive draws the tracer merged; traces carry no vertex counts.
"""
from __future__ import annotations

import glob
import json
import os
import re
from collections import defaultdict
from pathlib import Path

from scripts.qa import coverage as cov

MAIN = cov.MAIN
HOME_WT = Path(os.path.expanduser("~")) / ".codex" / "worktrees"
_FAM = re.compile(r"\[hit material families\]\s+vs/fs ([0-9a-f]{16})/([0-9a-f]{16}) draws (\d+) instanced (\d+) vertices (\d+)")
_SAVE = re.compile(r"\[save\] listing shows (?:\S+?__)?(.+?)(?: \(\d+ hidden|$)")


def scene_ids() -> dict:
    return cov.scene_ids()


def scene_of_save(save: str, sids: dict) -> str:
    for sid, sub in sids.items():
        if sub and (sub in save or save in sub):
            return sid
    return save or "?"


def captures() -> list[Path]:
    caps = list(cov.find_captures())
    for d in sorted(glob.glob(str(HOME_WT / "*" / "BG3RTX" / "captures" / "2026*"))):
        d = Path(d)
        if (d / "frame.json").exists() or (d / "frame_on0.json").exists():
            caps.append(d)
    seen, out = set(), []
    for c in caps:
        k = os.path.normcase(str(c.resolve()))
        if k not in seen:
            seen.add(k)
            out.append(c)
    return out


def _scene(cap: Path, sids: dict) -> str:
    rj = cap / "run.json"
    if rj.exists():
        try:
            return scene_of_save(json.loads(rj.read_text(encoding="utf-8-sig")).get("save", "") or "", sids)
        except Exception:
            pass
    return "?"


def pass_kinds(frame) -> list[tuple[str, dict]]:
    """(kind, pass) per render pass, with coverage.classify's kinds; b10 -> effects/forward."""
    ps = cov.passes_of(frame)
    if not ps:
        return []
    gb = [p for p in ps if len(p["color"]) >= 5 and p["color"][0] == cov.F_RG16F]
    sizes = {}
    for p in ps:
        if p["color"]:
            sizes[(p["w"], p["h"])] = sizes.get((p["w"], p["h"]), 0) + 1
    render = (gb[0]["w"], gb[0]["h"]) if gb else max(sizes, key=sizes.get)
    output = max(sizes, key=lambda s: s[0] * s[1])
    out, after, seen_gb = [], False, False
    for p in ps:
        if p["w"] > render[0] and p["color"] and p["color"][0] == cov.F_RGB10A2:
            after = True
        k = cov.classify(p, render, output, after)
        if k == "G-buffer":
            seen_gb = True
        if k == "b10":
            k = "forward" if seen_gb else "effects"
        out.append((k, p))
    return out


def trace_evidence(caps=None, log=print) -> dict:
    """{(vs, fs): {"draws", "gbuffer_draws", "passes": {kind}, "scenes": {s}, "captures"}} and totals."""
    sids = scene_ids()
    caps = captures() if caps is None else caps
    pairs = defaultdict(lambda: {"draws": 0, "gbuffer_draws": 0, "passes": set(), "scenes": set(), "captures": 0})
    per_capture = []    # (scene, {pair: gbuffer draws})
    n = 0
    for cap in caps:
        fr = cov.load_frame(cap)
        if not fr:
            continue
        n += 1
        sc = _scene(cap, sids)
        mine = defaultdict(int)
        seen = set()
        for k, p in pass_kinds(fr):
            for fs, vs, cnt in p["draws"]:
                key = (vs, fs)
                e = pairs[key]
                e["draws"] += cnt
                e["passes"].add(k)
                e["scenes"].add(sc)
                if k == "G-buffer":
                    e["gbuffer_draws"] += cnt
                    mine[key] += cnt
                if key not in seen:
                    seen.add(key)
                    e["captures"] += 1
        per_capture.append((sc, dict(mine)))
    log(f"frames: {n} traces, {len(pairs)} VS/FS pairs")
    return {"pairs": pairs, "per_capture": per_capture, "traces": n}


def gbuffer_share(ev: dict) -> dict:
    """Pair -> mean over scenes of (mean over that scene's traces of its G-buffer draw share).
    Only traces of a test scene (S1-S14) count when there are any."""
    by_scene = defaultdict(list)
    known = [x for x in ev["per_capture"] if re.fullmatch(r"S\d+", x[0])]
    for sc, m in known or ev["per_capture"]:
        tot = sum(m.values())
        if tot:
            by_scene[sc].append({k: v / tot for k, v in m.items()})
    out = defaultdict(float)
    for sc, lst in by_scene.items():
        for m in lst:
            for k, v in m.items():
                out[k] += v / len(lst) / len(by_scene)
    return dict(out), sorted(by_scene)


def log_files() -> list[Path]:
    pats = [MAIN / "bg3rtx*.log", MAIN / ".claude" / "worktrees" / "*" / "bg3rtx*.log",
            HOME_WT / "*" / "BG3RTX" / "bg3rtx*.log"]
    out = []
    for p in pats:
        out += [Path(x) for x in glob.glob(str(p))]
    return out


def log_vertices(files=None) -> dict:
    """(vs, fs) -> {"vertices": max per log line, "draws", "instanced", "scenes": {s}} from declined-pair lines."""
    sids = scene_ids()
    out = defaultdict(lambda: {"vertices": 0, "draws": 0, "instanced": 0, "scenes": set()})
    for f in files if files is not None else log_files():
        save = ""
        try:
            fh = open(f, encoding="utf-8", errors="replace")
        except OSError:
            continue
        with fh:
            for line in fh:
                if "[save] listing shows" in line:
                    m = _SAVE.search(line)
                    if m:
                        save = m.group(1).strip()
                    continue
                if "[hit material families]" not in line:
                    continue
                m = _FAM.search(line)
                if not m:
                    continue
                key = (m.group(1), m.group(2))
                e = out[key]
                v = int(m.group(5))
                if v > e["vertices"]:
                    e.update(vertices=v, draws=int(m.group(3)), instanced=int(m.group(4)))
                e["scenes"].add(scene_of_save(save, sids))
    return dict(out)
