"""Offline UV-slot check over traced VS/FS pairs (captures/*/frame.json) and dumped SPIR-V
(captures/shaders). Mirrors layer/spirv_xfb.h: patch_vs's UV choice (the lowest-location float2
Output) against stage_io/uv_match (the FS's lowest float2 Input). TRACK_D 13.19.

    python scripts/qa/uv_slots.py captures/shaders captures [more capture roots]
"""
import glob, json, os, struct, sys, collections

SH = sys.argv[1]
caps = sys.argv[2:]


def parse(path):
    b = open(path, 'rb').read()
    w = struct.unpack('<%dI' % (len(b) // 4), b)
    if w[0] != 0x07230203:
        return None
    p = 5
    model = -1
    f32, vec, ptr, vars_, loc, cmp, bi, xfbdec = set(), {}, {}, [], {}, {}, set(), set()
    while p < len(w):
        n, op = w[p] >> 16, w[p] & 0xffff
        if n == 0:
            return None
        a = w[p:p + n]
        if op == 15 and model < 0: model = a[1]
        if op == 22 and n >= 3 and a[2] == 32: f32.add(a[1])
        if op == 23 and n >= 4 and a[2] in f32: vec[a[1]] = a[3]
        if op == 32 and n >= 4 and a[2] in (1, 3): ptr[a[1]] = (a[2], a[3])
        if op == 59 and n >= 4 and a[3] in (1, 3): vars_.append((a[2], a[1], a[3] == 3))
        if op == 71 and n >= 4 and a[2] == 30: loc[a[1]] = a[3]
        if op == 71 and n >= 4 and a[2] == 31: cmp[a[1]] = a[3]
        if op == 71 and n >= 3 and a[2] == 11: bi.add(a[1])
        if op == 71 and n >= 3 and a[2] in (35, 36): xfbdec.add(a[1])
        p += n
    io = []
    for vid, t, out in vars_:
        if vid in bi or vid not in loc: continue
        pt = ptr.get(t)
        comps = 0
        if pt:
            if pt[1] in f32: comps = 1
            comps = vec.get(pt[1], comps)
        io.append(dict(loc=loc[vid], comp=cmp.get(vid, 0), comps=comps, out=out, id=vid,
                       skip=vid in cmp or vid in xfbdec))
    return model, io


cache = {}


def mod(h):
    if h not in cache:
        f = os.path.join(SH, h + '.spv')
        cache[h] = parse(f) if os.path.exists(f) else None
    return cache[h]


pairs = collections.Counter()
for c in caps:
    for f in glob.glob(os.path.join(c, '*', 'frame.json')):
        try:
            j = json.load(open(f, encoding='utf-8', errors='replace'))
        except Exception:
            continue
        stack = [j]
        while stack:
            o = stack.pop()
            if isinstance(o, dict):
                if isinstance(o.get('vs'), str) and isinstance(o.get('fs'), str) and o['vs'] and o['fs']:
                    pairs[(o['vs'], o['fs'])] += 1
                stack.extend(o.values())
            elif isinstance(o, list):
                stack.extend(o)

res = collections.Counter()
bad = []
missing = 0
for (vs, fs), n in pairs.items():
    v, fr = mod(vs), mod(fs)
    if not v or not fr or v[0] != 0 or fr[0] != 4:
        missing += 1
        continue
    outs = [s for s in v[1] if s['out'] and s['comps'] == 2]
    uv = min((s['loc'] for s in outs), default=-1)
    cand = [s for s in outs if s['loc'] == uv]
    if cand and cand[0]['skip']: uv = -1
    fin = [s for s in fr[1] if not s['out'] and s['comps'] == 2 and s['comp'] == 0]
    fs_uv = min((s['loc'] for s in fin), default=-1)
    if fs_uv < 0:
        res['FS without float2'] += 1; continue
    vsc = next((s['comps'] for s in v[1] if s['out'] and s['loc'] == fs_uv and s['comp'] == 0), 0)
    if uv < 0:
        res['nothing captured'] += 1; continue
    if uv == fs_uv:
        res['ok'] += 1
    else:
        k = 'VS wider at FS UV' if vsc > 2 else 'FS reads elsewhere'
        res[k] += 1
        bad.append((vs, fs, uv, fs_uv, vsc, n))
print(f"{len(pairs)} distinct VS/FS pairs in {len(caps)} capture roots; {missing} without both modules dumped")
for k, v in res.most_common(): print(f"  {k}: {v}")
for b in sorted(bad, key=lambda x: -x[5])[:40]:
    print("  MISMATCH vs %s fs %s: captured loc %d, FS UV loc %d (VS writes %d comps there), %d draws" % b)
