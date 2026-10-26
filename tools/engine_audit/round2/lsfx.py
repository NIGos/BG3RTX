"""Census of effect component types in Effects.pak .lsfx and Light component property names."""
import sys, os, json, collections
sys.path.insert(0, r"C:\Users\quali\AppData\Local\Temp\bg3rtx_audit")
from lsf import DATA, pak_index, pak_read, parse_lsf
pp = os.path.join(DATA, "Effects.pak"); idx = pak_index(pp)
types = collections.Counter(); files_with_light = 0; lprops = collections.Counter(); samp = {}; n = 0
for k in idx:
    if not k.endswith(".lsfx"): continue
    try: nodes = parse_lsf(pak_read(pp, idx[k]))[1]
    except Exception: continue
    n += 1; hl = False
    for i, nd in enumerate(nodes):
        if nd["name"] == "EffectComponent":
            t = nd["attrs"].get("Type"); types[t] += 1
            if t and "light" in t.lower():
                hl = True
                for c in nd["children"]:
                    for p in nodes[c]["children"]:
                        a = nodes[p]["attrs"]; fn = a.get("FullName") or a.get("AttributeName"); lprops[(t, fn)] += 1
                        samp.setdefault((t, fn), a.get("Value"))
    files_with_light += hl
print("lsfx parsed", n, "with light comps", files_with_light)
print("component types", types.most_common(40))
for (t, fn), c in lprops.most_common(60): print(f"  {t}:{fn} n={c} e.g. {json.dumps(samp[(t,fn)])[:80]}")
