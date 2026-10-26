"""Census of trigger GameObjects across all Triggers/_merged.lsf; maps TemplateName->root template TriggerType."""
import sys, os, json, collections
sys.path.insert(0, r"C:\Users\quali\AppData\Local\Temp\bg3rtx_audit")
from lsf import DATA, pak_index, pak_read, parse_lsf
attrs = collections.Counter(); kids = collections.Counter(); tmpl = collections.Counter(); samples = {}
nfiles = 0; nobj = 0
# root templates: collect trigger templates
rt = {}
for pak in ["Shared.pak", "Gustav.pak", "GustavX.pak"]:
    pp = os.path.join(DATA, pak); idx = pak_index(pp)
    for k in idx:
        if k.endswith("RootTemplates/_merged.lsf"):
            ver, nodes = parse_lsf(pak_read(pp, idx[k]))
            for i, n in enumerate(nodes):
                if n["name"] == "GameObjects" and n["attrs"].get("Type") == "trigger":
                    a = n["attrs"]
                    rt[a.get("MapKey")] = (a.get("Name"), a.get("TriggerType"), [nodes[c]["name"] for c in n["children"]])
    for k in idx:
        if not k.endswith("Triggers/_merged.lsf"): continue
        nfiles += 1
        ver, nodes = parse_lsf(pak_read(pp, idx[k]))
        for i, n in enumerate(nodes):
            if n["name"] != "GameObjects": continue
            nobj += 1
            for a in n["attrs"]: attrs[a] += 1
            ch = [nodes[c]["name"] for c in n["children"]]
            for c in ch: kids[c] += 1
            t = n["attrs"].get("TemplateName"); tmpl[t] += 1
            tt = rt.get(t, (None, None))[1]
            if tt and tt not in samples: samples[tt] = (pak, k, i)
print("trigger files", nfiles, "objects", nobj, "trigger roottemplates", len(rt))
print("attrs", attrs.most_common(60))
print("child nodes", kids.most_common(60))
bytype = collections.Counter()
for t, c in tmpl.items(): bytype[rt.get(t, ("?", "?"))[1]] += c
print("by TriggerType", bytype.most_common())
rtt = collections.Counter(v[1] for v in rt.values()); print("roottemplate TriggerTypes", rtt.most_common())
json.dump({"samples": samples, "rt": rt}, open("trig.json", "w"))
