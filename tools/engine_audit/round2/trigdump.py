import sys, os, json
sys.path.insert(0, r"C:\Users\quali\AppData\Local\Temp\bg3rtx_audit")
from lsf import DATA, pak_index, pak_read, parse_lsf
d = json.load(open("trig.json")); S = d["samples"]
for tt in sys.argv[1:]:
    pak, k, i = S[tt]; pp = os.path.join(DATA, pak)
    nodes = parse_lsf(pak_read(pp, pak_index(pp)[k]))[1]
    print("=====", tt, pak, k)
    def pr(j, dd):
        if nodes[j]["name"] in ("LayerList",): return
        print("  " * dd + nodes[j]["name"], json.dumps(nodes[j]["attrs"])[:500])
        for c in nodes[j]["children"][:6]: pr(c, dd + 1)
    pr(i, 0)
    rtk = [k2 for k2, v in d["rt"].items() if v[1] == tt]; print("roottemplate", rtk, d["rt"][rtk[0]])
