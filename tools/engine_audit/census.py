import sys, os, json, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lsf import DATA, pak_index, pak_read, parse_lsf

pak = sys.argv[1]; pat = sys.argv[2]
pp = os.path.join(DATA, pak); idx = pak_index(pp)
files = [k for k in idx if pat in k and k.endswith(".lsf")]
types = collections.Counter(); keys = collections.defaultdict(collections.Counter)
nodenames = collections.Counter(); samples = {}
for k in files:
    try:
        ver, nodes = parse_lsf(pak_read(pp, idx[k]))
    except Exception as ex:
        print("ERR", k, ex); continue
    for nd in nodes:
        nodenames[nd["name"]] += 1
        if nd["name"] == "GameObjects":
            t = nd["attrs"].get("Type", "?"); types[t] += 1
            for a in nd["attrs"]: keys[t][a] += 1
            for c in nd["children"]:
                keys[t]["/" + nodes[c]["name"]] += 1
            samples.setdefault(t, {kk: vv for kk, vv in nd["attrs"].items()})
print("files", len(files))
print("types", dict(types))
print("nodes", dict(nodenames.most_common(40)))
for t, c in keys.items():
    print("==", t, dict(c.most_common(80)))
if len(sys.argv) > 3:
    for t, s in samples.items(): print("SAMPLE", t, json.dumps(s)[:1500])
