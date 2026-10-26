"""Turns a BG3RTX frame trace into a readable report.

    python scripts/analyze_trace.py                 # latest capture
    python scripts/analyze_trace.py captures/<ts>   # a specific one
    python scripts/analyze_trace.py --disasm        # also decompile the shaders it references

Writes report.md (pass list in submission order, resource producers/consumers,
heuristic guesses for the G-buffer, DLSS boundary and UI composite) and
graph.dot next to frame.json.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

FORMATS = {
    0: "UNDEFINED", 9: "R8_UNORM", 16: "R8G8_UNORM", 37: "R8G8B8A8_UNORM", 43: "R8G8B8A8_SRGB",
    44: "B8G8R8A8_UNORM", 50: "B8G8R8A8_SRGB", 64: "A2B10G10R10_UNORM_PACK32", 58: "A2R10G10B10_UNORM_PACK32",
    70: "R16_UNORM", 74: "R16_UINT", 76: "R16_SFLOAT", 77: "R16G16_UNORM", 83: "R16G16_SFLOAT",
    91: "R16G16B16A16_UNORM", 97: "R16G16B16A16_SFLOAT", 98: "R32_UINT", 99: "R32_SINT", 100: "R32_SFLOAT",
    101: "R32G32_UINT", 103: "R32G32_SFLOAT", 107: "R32G32B32A32_UINT", 109: "R32G32B32A32_SFLOAT",
    122: "B10G11R11_UFLOAT_PACK32", 123: "E5B9G9R9_UFLOAT_PACK32", 124: "D16_UNORM", 125: "X8_D24_UNORM_PACK32",
    126: "D32_SFLOAT", 127: "S8_UINT", 129: "D24_UNORM_S8_UINT", 130: "D32_SFLOAT_S8_UINT",
    131: "BC1_RGB_UNORM", 133: "BC1_RGBA_UNORM", 137: "BC3_UNORM", 139: "BC4_UNORM", 141: "BC5_UNORM",
    143: "BC6H_UFLOAT", 145: "BC7_UNORM", 146: "BC7_SRGB",
}
DEPTH = {124, 125, 126, 127, 128, 129, 130}


def fmt(n):
    return FORMATS.get(n, str(n))


def latest_capture():
    caps = sorted(p for p in (ROOT / "captures").glob("*") if (p / "frame.json").exists())
    if not caps:
        sys.exit("no captures/<stamp>/frame.json yet: press Ctrl+Shift+F10 in game or create trace.request")
    return caps[-1]


def flatten(frame):
    """Yields (submit_index, cmd_index, event) in execution order."""
    for si, sub in enumerate(frame["submits"]):
        for ci, cmd in enumerate(sub["cmds"]):
            if not cmd["traced"]:
                yield si, ci, {"op": "untraced_cmd", "cmd": cmd["cmd"]}
                continue
            for ev in cmd["events"]:
                yield si, ci, ev


def build_passes(frame):
    """Groups events into units: render passes, and runs of compute/transfer between them."""
    units, cur = [], None
    for si, ci, ev in flatten(frame):
        op = ev["op"]
        if op == "pass":
            cur = {"kind": "pass", "submit": si, "cmd": ci, "w": ev["w"], "h": ev["h"], "color": ev["color"],
                   "depth": ev["depth"], "segs": []}
            units.append(cur)
        elif op == "end":
            if cur:
                cur["draws"] = ev.get("draws", 0)
                cur["dumps"] = ev.get("dumps", [])
            cur = None
        elif op in ("draw", "draw_indirect"):
            if cur is None:
                cur = {"kind": "orphan", "submit": si, "cmd": ci, "segs": []}
                units.append(cur)
            cur["segs"].append(ev)
        else:  # dispatch, copy, blit, clear, ...
            units.append({"kind": op, "submit": si, "cmd": ci, "segs": [ev]})
    return units


def image_desc(images, ref):
    im = images.get(ref)
    if not im:
        return ref or "-"
    tag = " swapchain" if im["swapchain"] else ""
    return f"{ref} {im['w']}x{im['h']} {fmt(im['fmt'])}{tag}"


def reads_of(unit):
    out = set()
    for s in unit["segs"]:
        for slot, v in s.get("res", {}).items():
            if ":" in v:
                mode, ref = v.split(":", 1)
                if mode in ("r", "in"):
                    out.add(ref)
    for k in ("src",):
        if unit["segs"] and unit["segs"][0].get(k):
            out.add(unit["segs"][0][k])
    return out


def writes_of(unit):
    out = set()
    if unit["kind"] == "pass":
        out.update(c for c in unit["color"] if c)
        if unit["depth"]:
            out.add(unit["depth"])
    for s in unit["segs"]:
        for v in s.get("res", {}).values():
            if v.startswith("rw:"):
                out.add(v[3:])
        if s.get("dst"):
            out.add(s["dst"])
    return out


def disasm(cap, hashes):
    tool = shutil.which("spirv-cross") or (os.environ.get("VULKAN_SDK", "") + "\\Bin\\spirv-cross.exe")
    src_dir = ROOT / "captures" / "shaders"
    out_dir = cap / "shaders"
    out_dir.mkdir(exist_ok=True)
    done = 0
    for h in sorted(hashes):
        spv = src_dir / f"{h}.spv"
        dst = out_dir / f"{h}.glsl"
        if not spv.exists() or dst.exists():
            continue
        r = subprocess.run([tool, str(spv), "--vulkan-semantics", "--output", str(dst)], capture_output=True, text=True)
        if r.returncode:
            dst.with_suffix(".err.txt").write_text(r.stderr)
        else:
            done += 1
    return done


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("capture", nargs="?")
    ap.add_argument("--disasm", action="store_true")
    a = ap.parse_args()
    cap = Path(a.capture) if a.capture else latest_capture()
    frame = json.loads((cap / "frame.json").read_text())
    images = frame["images"]
    units = build_passes(frame)
    dumps_path = cap / "dumps.json"
    dumps = json.loads(dumps_path.read_text()) if dumps_path.exists() else {}

    producers, consumers = defaultdict(list), defaultdict(list)
    for i, u in enumerate(units):
        for r in writes_of(u):
            producers[r].append(i)
        for r in reads_of(u):
            consumers[r].append(i)

    swap = {k for k, v in images.items() if v["swapchain"]}
    sizes = defaultdict(int)
    for u in units:
        if u["kind"] == "pass":
            sizes[(u["w"], u["h"])] += 1
    out_size = next(((images[s]["w"], images[s]["h"]) for s in swap), None)

    lines = [f"# Frame {frame['frame']} — {cap.name}", ""]
    lines.append(f"{len(frame['submits'])} submits, {len(units)} units, {len(images)} images referenced. "
                 f"Output size: {out_size}.")
    lines.append("")
    lines.append("Pass sizes: " + ", ".join(f"{w}x{h} ×{n}" for (w, h), n in sorted(sizes.items(), key=lambda x: -x[1])))
    lines.append("")

    # Heuristics.
    guesses = []
    for i, u in enumerate(units):
        if u["kind"] != "pass":
            continue
        if len(u["color"]) >= 3 and u["depth"]:
            guesses.append(f"- unit {i}: **G-buffer candidate** ({len(u['color'])} color targets + depth, {u['w']}x{u['h']})")
        if any(c in swap for c in u["color"]) and len(u["segs"]) <= 3:
            guesses.append(f"- unit {i}: **writes the swapchain** — UI composite / final blit candidate")
    for i, u in enumerate(units):
        if u["kind"] == "dispatch" and out_size:
            ws = [images.get(r) for r in writes_of(u)]
            rs = [images.get(r) for r in reads_of(u)]
            if any(w and (w["w"], w["h"]) == out_size for w in ws) and any(
                    r and (r["w"], r["h"]) != out_size and r["w"] > 0 and r["fmt"] in (83, 122, 97) for r in rs):
                guesses.append(f"- unit {i}: dispatch reading render-res and writing output-res — **DLSS evaluate candidate**")
    if guesses:
        lines += ["## Guesses", ""] + guesses + [""]

    lines += ["## Units in execution order", "",
              "| # | sub/cmd | kind | size | writes | reads | shaders | n | dumps |", "|---|---|---|---|---|---|---|---|---|"]
    shader_hashes = set()
    for i, u in enumerate(units):
        if u["kind"] == "untraced_cmd":
            continue
        segs = u["segs"]
        sh = []
        n = 0
        for s in segs:
            n += s.get("n", 1)
            for k in ("vs", "fs", "cs"):
                if s.get(k) and s[k] != "0" * 16:
                    shader_hashes.add(s[k])
            if s.get("fs") and s["fs"] != "0" * 16:
                sh.append(s["fs"][:8])
            elif s.get("cs"):
                sh.append("cs:" + s["cs"][:8])
        uniq = list(dict.fromkeys(sh))
        size = f"{u['w']}x{u['h']}" if u["kind"] == "pass" else ""
        w = "<br>".join(image_desc(images, r) for r in sorted(writes_of(u)))
        r = "<br>".join(image_desc(images, x) for x in sorted(reads_of(u)))[:600]
        shaders = " ".join(uniq[:6]) + (f" +{len(uniq) - 6}" if len(uniq) > 6 else "")
        dl = " ".join(d for d in u.get("dumps", []) if d in dumps)
        lines.append(f"| {i} | {u['submit']}/{u['cmd']} | {u['kind']} | {size} | {w} | {r} | {shaders} | {n} | {dl} |")

    lines += ["", "## Render targets: who writes, who reads", "", "| image | written by | read by |", "|---|---|---|"]
    for ref in sorted(producers, key=lambda r: producers[r][0]):
        lines.append(f"| {image_desc(images, ref)} | {producers[ref][:12]} | {consumers.get(ref, [])[:16]} |")

    if dumps:
        unit_of = {d: i for i, u in enumerate(units) for d in u.get("dumps", [])}
        lines += ["", "## Dumps (attachment contents after the pass)", "",
                  "| dump | unit | slot | image | fmt | min | max | mean | nonfinite | png |",
                  "|---|---|---|---|---|---|---|---|---|---|"]
        g = lambda v: "[" + ", ".join("-" if x is None else f"{x:.4g}" for x in v) + "]"
        for k in sorted(dumps, key=lambda d: int(d[1:])):
            d = dumps[k]
            lines.append(f"| {k} | {unit_of.get(k, '?')} | {d['slot']} | {d['image']} {d['w']}x{d['h']} | {d['fmt']} | "
                         f"{g(d['min'])} | {g(d['max'])} | {g(d['mean'])} | {d['nonfinite']} | {d['png']} |")

    (cap / "report.md").write_text("\n".join(lines), encoding="utf-8")

    dot = ["digraph frame {", "rankdir=LR; node [shape=box,fontsize=9];"]
    for i, u in enumerate(units):
        if u["kind"] == "untraced_cmd":
            continue
        label = f"{i} {u['kind']}" + (f"\\n{u['w']}x{u['h']}" if u["kind"] == "pass" else "")
        dot.append(f'u{i} [label="{label}"];')
    for ref, ws in producers.items():
        for c in consumers.get(ref, []):
            prev = [w for w in ws if w < c]
            if prev:
                dot.append(f'u{prev[-1]} -> u{c} [label="{ref}",fontsize=7];')
    dot.append("}")
    (cap / "graph.dot").write_text("\n".join(dot))

    print(f"{cap / 'report.md'} written ({len(units)} units, {len(shader_hashes)} distinct shaders)")
    if a.disasm:
        print(f"{disasm(cap, shader_hashes)} shaders decompiled into {cap / 'shaders'}")


if __name__ == "__main__":
    main()
