"""Census of bank root names across Content/_merged.lsf in given paks; dump one resource per target bank."""
import sys, os, json, collections
sys.path.insert(0, r"C:\Users\quali\AppData\Local\Temp\bg3rtx_audit")
from lsf import DATA, pak_index, pak_read, parse_lsf
roots = collections.Counter(); where = collections.defaultdict(list)
for pak in ["Shared.pak", "Gustav.pak", "GustavX.pak", "Engine.pak", "Game.pak"]:
    pp = os.path.join(DATA, pak); idx = pak_index(pp)
    for k in idx:
        if "/Content/" not in k or not k.endswith("_merged.lsf"): continue
        try: ver, nodes = parse_lsf(pak_read(pp, idx[k]))
        except Exception as e: continue
        for r in [i for i, n in enumerate(nodes) if n["parent"] < 0]:
            b = nodes[r]["name"]; n = len(nodes[r]["children"])
            roots[b] += n
            if n: where[b].append((pak, k, n))
for b, n in roots.most_common(): print(b, n, len(where[b]), where[b][:2])
json.dump(where, open(r"C:\Users\quali\AppData\Local\Temp\bg3rtx_audit2\roots.json", "w"), indent=0)
