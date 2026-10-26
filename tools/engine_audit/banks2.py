import sys, os, json, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lsf import DATA, pak_index, pak_read, parse_lsf
banks = collections.Counter(); where = collections.defaultdict(list)
matparams = collections.Counter(); emiss = collections.Counter(); atm = None; vis = None; mat = None
for pak in sys.argv[1].split(","):
    pp = os.path.join(DATA, pak); idx = pak_index(pp)
    for k in idx:
        if "/Content/" not in k or not k.endswith("_merged.lsf"): continue
        try: ver, nodes = parse_lsf(pak_read(pp, idx[k]))
        except Exception as ex: continue
        for c in [i for i, n in enumerate(nodes) if n["parent"] < 0]:
            if True:
                b = nodes[c]["name"]; n = len(nodes[c]["children"]); banks[b] += n
                if len(where[b]) < 2: where[b].append(f"{pak}:{k}")
                if b == "MaterialBank":
                    for res in nodes[c]["children"]:
                        for p in nodes[res]["children"]:
                            for q in nodes[p]["children"]:
                                pn = nodes[q]["attrs"].get("ParameterName")
                                if pn:
                                    matparams[(nodes[q]["name"], pn)] += 1
                                    if "miss" in pn.lower() or "glow" in pn.lower() or "illum" in pn.lower(): emiss[(nodes[q]["name"], pn)] += 1
                        if mat is None and len(nodes[res]["children"]) > 0:
                            def tree(i, d=0):
                                o = {"_": nodes[i]["name"], **nodes[i]["attrs"]}
                                if d < 4 and nodes[i]["children"]: o["c"] = [tree(x, d + 1) for x in nodes[i]["children"][:40]]
                                return o
                            mat = tree(res)
                if b == "VisualBank" and vis is None and nodes[c]["children"]:
                    res = nodes[c]["children"][0]
                    def tree(i, d=0):
                        o = {"_": nodes[i]["name"], **nodes[i]["attrs"]}
                        if d < 3 and nodes[i]["children"]: o["c"] = [tree(x, d + 1) for x in nodes[i]["children"][:8]]
                        return o
                    vis = tree(res)
print("banks", dict(banks))
for b, w in where.items(): print(" ", b, w[:1])
print("material param kinds", len(matparams))
print("top params", [(f"{a}:{b}", n) for (a, b), n in matparams.most_common(50)])
print("emissive-like", [(f"{a}:{b}", n) for (a, b), n in emiss.most_common(30)])
print("MATERIAL SAMPLE", json.dumps(mat)[:3000])
print("VISUAL SAMPLE", json.dumps(vis)[:2500])

