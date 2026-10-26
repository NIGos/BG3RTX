"""Dump a compact tree of an LSF: first N resources of a bank, attribute names + values, depth-limited."""
import sys, os, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lsf import DATA, pak_index, pak_read, parse_lsf
pak, path = sys.argv[1], sys.argv[2]
maxres = int(sys.argv[3]) if len(sys.argv) > 3 else 1
filt = sys.argv[4] if len(sys.argv) > 4 else None
pp = os.path.join(DATA, pak); idx = pak_index(pp)
ver, nodes = parse_lsf(pak_read(pp, idx[path]))
def pr(i, d, lim=[0]):
    nd = nodes[i]
    lim[0] += 1
    if lim[0] > 4000: return
    a = {k: (v if not isinstance(v, str) or len(v) < 80 else v[:80]) for k, v in nd["attrs"].items()}
    print("  " * d + nd["name"], json.dumps(a)[:600])
    for c in nd["children"]: pr(c, d + 1)
roots = [i for i, n in enumerate(nodes) if n["parent"] < 0]
print("ver", ver, "nodes", len(nodes))
for r in roots:
    print(nodes[r]["name"])
    for c in nodes[r]["children"]:
        print(" ", nodes[c]["name"], "children:", len(nodes[c]["children"]))
        shown = 0
        for cc in nodes[c]["children"]:
            if filt and filt not in json.dumps(nodes[cc]["attrs"]): continue
            pr(cc, 2); shown += 1
            if shown >= maxres: break
