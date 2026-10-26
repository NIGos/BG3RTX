import sys, os, json, collections, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lsf import DATA, pak_index, pak_read, parse_lsf

pp = os.path.join(DATA, "Gustav.pak"); idx = pak_index(pp)
files = [k for k in idx if k.endswith("/Lights/_merged.lsf") or k.endswith("/CombinedLights/_merged.lsf")]
tot = collections.Counter(); per = {}
attrc = collections.Counter(); lt = collections.Counter()
near = []
cam = [float(x) for x in sys.argv[1].split(",")] if len(sys.argv) > 1 else None
for k in files:
    ver, nodes = parse_lsf(pak_read(pp, idx[k]))
    lv = k.split("/")[3]; n = 0
    for i, nd in enumerate(nodes):
        if nd["name"] != "GameObjects": continue
        n += 1; a = nd["attrs"]
        for kk in a: attrc[kk] += 1
        lt[a.get("LightType", "tmpl")] += 1
        pos = None
        for c in nd["children"]:
            if nodes[c]["name"] == "Transform": pos = nodes[c]["attrs"].get("Position")
        if cam and pos and math.dist(pos, cam) < 60: near.append((lv, a.get("Name"), pos, a.get("Radius"), a.get("Intensity")))
    tot[k.split("/")[4]] += n; per[lv + "/" + k.split("/")[4]] = n
print("files", len(files), "lights", dict(tot))
print("top levels", sorted(per.items(), key=lambda x: -x[1])[:15])
print("attr freq", dict(attrc.most_common(60)))
print("LightType overrides", dict(lt))
print("near", len(near)); [print(x) for x in near[:15]]
