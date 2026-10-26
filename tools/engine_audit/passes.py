import json, sys, collections
d = sys.argv[1]
j = json.load(open(d + "/frame.json"))
imgs = j["images"]
def fmt(i):
    m = imgs.get(i[1:] if i.startswith("i") else i) if isinstance(imgs, dict) else None
    if m is None and isinstance(imgs, dict): m = imgs.get(i)
    if isinstance(m, dict): return f"{m.get('format', m.get('fmt','?'))}:{m.get('w', m.get('width','?'))}x{m.get('h', m.get('height','?'))}"
    return "?"
cur = None; out = []
def flush():
    if cur: out.append(cur)
for s in j["submits"]:
    for c in s["cmds"]:
        for e in c["events"]:
            op = e.get("op")
            if op == "pass":
                flush(); cur = dict(kind="pass", color=e["color"], depth=e["depth"], size=f"{e['w']}x{e['h']}", draws=0, fs=collections.Counter(), vs=collections.Counter(), tex=set())
            elif op == "draw" and cur is not None:
                cur["draws"] += e.get("n", 1); cur["fs"][e.get("fs")] += 1; cur["vs"][e.get("vs")] += 1
                cur["tex"] |= {k for k in e.get("res", {})}
            elif op == "dispatch":
                flush(); cur = None
                out.append(dict(kind="cs", cs=e["cs"], groups=e.get("groups"), res=sorted(e.get("res", {}).keys())[:12]))
            elif op in ("copy", "blit", "copy_buffer", "clear", "resolve"):
                out.append(dict(kind=op, **{k: v for k, v in e.items() if k != "op"}))
flush()
if sys.argv[2:] == ["imgs"]:
    k = list(imgs.items())[:3] if isinstance(imgs, dict) else imgs[:3]; print(k); sys.exit()
for i, u in enumerate(out):
    if u["kind"] == "pass":
        print(i, "PASS", u["size"], "col", [c + "(" + fmt(c) + ")" for c in u["color"]], "dep", u["depth"], "draws", u["draws"],
              "fs", len(u["fs"]), u["fs"].most_common(4), "slots", sorted(u["tex"])[:14])
    elif u["kind"] == "cs":
        print(i, "CS", u["cs"][:8], u["groups"], u["res"][:8])
    else:
        print(i, u["kind"].upper(), {k: v for k, v in u.items() if k != "kind"})
