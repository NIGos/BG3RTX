"""Census of rendering flags on GameObjects in level Scenery/Items/RootTemplates/TileConstructions _merged.lsf."""
import sys, os, json, collections, re
sys.path.insert(0, r"C:\Users\quali\AppData\Local\Temp\bg3rtx_audit")
from lsf import DATA, pak_index, pak_read, parse_lsf
pat = re.compile(r"shadow|decal|channel|fade|roof|seethrough|see_through|cull|lod|render|occlu|blocker|hide|visib|glow|emiss|light|wall|floor|camera", re.I)
stats = collections.defaultdict(collections.Counter); nobj = collections.Counter(); childn = collections.Counter()
for pak in ["Shared.pak", "Gustav.pak", "GustavX.pak"]:
    pp = os.path.join(DATA, pak); idx = pak_index(pp)
    for k in idx:
        m = re.search(r"/(Scenery|Items|RootTemplates|TileConstructions|Characters)/_merged\.lsf$", k)
        if not m: continue
        cat = m.group(1)
        try: nodes = parse_lsf(pak_read(pp, idx[k]))[1]
        except Exception: continue
        for nd in nodes:
            if nd["name"] != "GameObjects": continue
            nobj[cat] += 1
            for a, v in nd["attrs"].items():
                if pat.search(a): stats[(cat, a)][json.dumps(v)[:40]] += 1
            for c in nd["children"]:
                cn = nodes[c]["name"]
                if pat.search(cn): childn[(cat, cn)] += 1
print("objects", nobj)
for (cat, a), c in sorted(stats.items(), key=lambda x: (x[0][0], -sum(x[1].values()))):
    print(f"{cat:18s} {a:40s} n={sum(c.values()):7d} {c.most_common(4)}")
print("child nodes", childn.most_common(40))
