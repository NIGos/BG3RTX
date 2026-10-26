import sys, os, json, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lsf import DATA, pak_index, pak_read, parse_lsf

pak, path = sys.argv[1], sys.argv[2]
pp = os.path.join(DATA, pak); idx = pak_index(pp)
ver, nodes = parse_lsf(pak_read(pp, idx[path]))
types = collections.Counter(); keys = collections.defaultdict(collections.Counter)
samples = collections.defaultdict(list)
lightref = collections.Counter()
for i, nd in enumerate(nodes):
    if nd["name"] != "GameObjects": continue
    t = nd["attrs"].get("Type", "?"); types[t] += 1
    for a, v in nd["attrs"].items():
        keys[t][a] += 1
        if "light" in a.lower() or "emiss" in a.lower(): lightref[(t, a)] += 1
    for c in nd["children"]:
        keys[t]["/" + nodes[c]["name"]] += 1
    if len(samples[t]) < 2: samples[t].append(nd["attrs"])
print("ver", ver, "nodes", len(nodes)); print("types", dict(types))
print("light-ish attrs", {f"{k[0]}.{k[1]}": v for k, v in lightref.items()})
want = sys.argv[3].split(",") if len(sys.argv) > 3 else ["light"]
for t in want:
    print("==", t, dict(keys[t].most_common(120)))
    for s in samples[t]: print("SAMPLE", json.dumps(s)[:2500])
