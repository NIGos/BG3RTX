import mmap, re
p = r"D:\SteamLibrary\steamapps\common\Baldurs Gate 3\bin\bg3.exe"
with open(p, "rb") as f:
    m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
    for s in [b"SDL_PollEvent", b"SDL_PumpEvents", b"PeekMessageW", b"GetRawInputData", b"SDL2.dll", b"vkQueueSubmit2", b"vkQueueSubmit",
              b"MaskedOcclusionCulling", b"SkinningCompute", b"RenderThread", b"Render Thread", b"GameThread", b"NvAPI", b"nvapi64",
              b"LowLatency", b"FeedbackBuffer", b"Feedback", b"OcclusionCulling", b"Occluder", b"FrustumCull", b"CullDistance",
              b"ShadowProxy", b"PhotoMode", b"RenderDistance", b"HLODDistance"]:
        h = [x.start() for x in re.finditer(re.escape(s), m)]
        c = [re.sub(rb"[^\x20-\x7e]", b".", m[max(0, x - 30):x + 60]).decode() for x in h[:2]]
        print(f"{s.decode():26s}{len(h):5d} {c}")
