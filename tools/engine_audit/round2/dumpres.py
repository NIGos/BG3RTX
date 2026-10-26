"""dumpres.py PAK PATH BANK [index|namefilter] : print full node tree of one resource in BANK."""
import sys, os, json
sys.path.insert(0, r"C:\Users\quali\AppData\Local\Temp\bg3rtx_audit")
from lsf import DATA, pak_index, pak_read, parse_lsf
pak, path, bank = sys.argv[1:4]; sel = sys.argv[4] if len(sys.argv) > 4 else "0"
pp = os.path.join(DATA, pak); idx = pak_index(pp)
ver, nodes = parse_lsf(pak_read(pp, idx[path]))
def pr(i, d):
    nd = nodes[i]
    a = {k: (v if not isinstance(v, str) or len(v) < 100 else v[:100]) for k, v in nd["attrs"].items()}
    print("  " * d + nd["name"], json.dumps(a))
    for c in nd["children"]: pr(c, d + 1)
for r in [i for i, n in enumerate(nodes) if n["parent"] < 0]:
    if nodes[r]["name"] != bank: continue
    res = nodes[r]["children"]
    print("count", len(res))
    if sel.isdigit(): pr(res[int(sel)], 0)
    elif sel == "names":
        for x in res: print(nodes[x]["attrs"].get("Name"), nodes[x]["attrs"].get("ID"))
    else:
        for x in res:
            if sel in str(nodes[x]["attrs"].get("Name")): pr(x, 0); break
