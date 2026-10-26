import mmap, re, sys, collections
p = r"D:\SteamLibrary\steamapps\common\Baldurs Gate 3\bin\bg3.exe"
with open(p, "rb") as f:
    m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
    pats = [rb"ls::LightComponent", rb"ls::TransformComponent", rb"ls::VisualComponent", rb"ecl::LightComponent",
            rb"ls::CameraComponent", rb"ecl::GameCameraBehavior", rb"ls::LevelComponent", rb"ls::EffectComponent",
            rb"rf::Camera", rb"GameRenderView", rb"ColorGradingLut", rb"ls::AtmosphereResource", rb"LightingResource",
            rb"ls::LightProbeComponent", rb"ls::CullComponent", rb"Granite", rb"GRANITE", rb"GTS", rb"NvLowLatency", rb"Reflex",
            rb"NVSDK_NGX_VULKAN_CreateFeature", rb"slReflex", rb"PCLSetMarker", rb"ls::PhysicsComponent",
            rb"AtmosphereResource", rb"ls::TimeOfDay", rb"TimeOfDay", rb"ExposureMin", rb"ToneMap", rb"Tonemap", rb"FogLayer",
            rb"IESProfile", rb"IES", rb"LightCookie", rb"Kelvin", rb"VirtualTexture", rb"ls::RenderableObject", rb"ls::Visual",
            rb"BlendShape", rb"SkinningBuffer", rb"Skinning", rb"HLOD", rb"CombinedLight", rb"ecs::EntityWorld"]
    for pat in pats:
        hits = [mm.start() for mm in re.finditer(re.escape(pat), m)]
        ctx = []
        for h in hits[:3]:
            s = m[max(0, h - 24):h + 72]
            ctx.append(re.sub(rb"[^\x20-\x7e]", b".", s).decode())
        print(f"{pat.decode():32s} {len(hits):5d}  {ctx[:2]}")
    # count RTTI type descriptors
    rtti = re.findall(rb"\.\?A[VU][A-Za-z0-9_@?$]{3,120}@@", m)
    print("RTTI type descriptors:", len(rtti))
    ns = collections.Counter(r.split(b"@")[-3] if r.count(b"@") >= 3 else b"" for r in rtti)
    print("top namespaces:", [(k.decode(errors='replace'), v) for k, v in ns.most_common(15)])
    for kw in [rb"Light", rb"Camera", rb"Atmosphere", rb"Visual", rb"Skeleton", rb"Lighting", rb"PostProcess"]:
        sel = sorted({r.decode() for r in rtti if kw in r})[:14]
        print(kw.decode(), len([r for r in rtti if kw in r]), sel)
