import sys, os, json, struct
sys.path.insert(0, r"C:\Users\quali\AppData\Local\Temp\bg3rtx_audit")
from lsf import DATA, pak_index, pak_read, parse_lsf
pp = os.path.join(DATA, "Gustav.pak"); idx = pak_index(pp)
L = "Mods/Gustav/Levels/WLD_Main_A/LightProbes/"
ver, nodes = parse_lsf(pak_read(pp, idx[L + "_merged.lsf"]))
def pr(i, d):
    if nodes[i]["name"] == "LayerList": return
    print("  " * d + nodes[i]["name"], json.dumps(nodes[i]["attrs"])[:700]); [pr(c, d + 1) for c in nodes[i]["children"]]
for r in [i for i, n in enumerate(nodes) if n["parent"] < 0]:
    print(nodes[r]["name"]); [pr(c, 1) for c in nodes[r]["children"][:2]]
FOURCC = lambda v: struct.pack("<I", v)
def dds(b, nm):
    h = struct.unpack_from("<4s7I", b, 0)
    magic, size, flags, H, W, pitch, depth, mips = h
    pf = struct.unpack_from("<2I4s5I", b, 76)
    caps = struct.unpack_from("<4I", b, 108)
    s = f"{nm}: {len(b)}B magic={magic} {W}x{H} depth={depth} mips={mips} pfFlags={pf[1]:#x} fourcc={pf[2]} rgbbits={pf[3]} caps2={caps[1]:#x}(cube={bool(caps[1]&0x200)})"
    if pf[2] == b"DX10":
        dxgi, dim, misc, asz, misc2 = struct.unpack_from("<5I", b, 128); s += f" DXGI={dxgi} dim={dim} misc={misc:#x} arraySize={asz}"
    print(s)
g = "3f38f4f7-6864-4443-bee9-2a6d849babd8_375af627-6f3c-19e8-1046-29265ae9e8f7"
for suf in ["_Diffuse_RGB.dds", "_Diffuse_Alpha.dds", "_Specular_RGB.dds", "_Specular_Alpha.dds"]:
    dds(pak_read(pp, idx[L + g + suf]), suf)
sh = pak_read(pp, idx[L + g + "_SH.dat"]); print("SH.dat", len(sh), sh.hex()); print("as floats", [round(x, 4) for x in struct.unpack(f"<{len(sh)//4}f", sh)])
