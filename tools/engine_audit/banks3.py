import sys, os, json, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lsf import DATA, pak_index, pak_read, parse_lsf
params = collections.Counter(); emiss = collections.Counter(); srcs = collections.Counter()
samples = {}; nmat = 0; nemat = 0
def tree(nodes, i, d=0, lim=60):
    o = {"_": nodes[i]["name"], **nodes[i]["attrs"]}
    if d < 5 and nodes[i]["children"]: o["c"] = [tree(nodes, x, d + 1, lim) for x in nodes[i]["children"][:lim]]
    return o
for pak in sys.argv[1].split(","):
    pp = os.path.join(DATA, pak); idx = pak_index(pp)
    for k in idx:
        if "/Content/" not in k or not k.endswith("_merged.lsf"): continue
        try: ver, nodes = parse_lsf(pak_read(pp, idx[k]))
        except Exception: continue
        for r in [i for i, n in enumerate(nodes) if n["parent"] < 0]:
            b = nodes[r]["name"]
            if b == "MaterialBank":
                for res in nodes[r]["children"]:
                    nmat += 1; srcs[os.path.basename(nodes[res]["attrs"].get("SourceFile", ""))] += 1
                    has = False
                    for q in nodes[res]["children"]:
                        pn = nodes[q]["attrs"].get("ParameterName")
                        if pn:
                            params[(nodes[q]["name"], pn)] += 1
                            if any(s in pn.lower() for s in ("emiss", "glow", "illum", "lava", "fire")):
                                emiss[(nodes[q]["name"], pn)] += 1; has = True
                    if has:
                        nemat += 1
                        samples.setdefault("emissive_material", tree(nodes, res))
            elif b in ("AtmosphereBank", "VirtualTextureBank", "VisualBank", "TextureBank") and b not in samples and nodes[r]["children"]:
                for res in nodes[r]["children"]:
                    t = tree(nodes, res)
                    if b != "VisualBank" or "Objects" in json.dumps(t):
                        samples[b] = t; break
print("materials", nmat, "with emissive-like params", nemat)
print("top sources", srcs.most_common(25))
print("top params", [(f"{a}:{b}", n) for (a, b), n in params.most_common(70)])
print("emissive-like", [(f"{a}:{b}", n) for (a, b), n in emiss.most_common(30)])
for k, v in samples.items(): print("SAMPLE", k, json.dumps(v)[:5000])
