"""Shader hash -> material template join (AUDIT_ENGINE_DATA R2 A1).

Every material template `Public/<mod>/Assets/Materials/<dir>/<T>.lsf` in Materials.pak is
compiled once per variant into `<T>_<ST|STI>_<PASS>_Vulkan.bshd`:

  ST / STI   Static / StaticInstanced (the template's `ShaderDesc_Static[Instanced]<Pass>`)
  PASS       DEF Deferred, FOR Forward, EMI Emissive, DEP Depth, DEPS DepthShadow,
             DEPST DepthShadowTetrahedron, VEL Velocity, BAKE Bake

Each bshd holds the variant's vertex and fragment SPIR-V; their FNV-1a 64 hashes are the
hashes the layer sees at vkCreateShaderModule. The template lists, per variant, the
Vulkan set/binding of each texture, sampler and virtual-texture slot and the constant
buffer offset of each scalar/vector parameter, so a runtime shader hash resolves to
"which binding holds the base colour, the emissive colour, the alpha cut-off...".

Engine shaders (EngineShaders.pak `Shaders/Vulkan/**.bshd`) get a name only.
"""
from __future__ import annotations

import os
import re
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass, field

from .. import lsf, spirv
from ..lspk import PackageSet

PASSES = {"DEF": "Deferred", "FOR": "Forward", "EMI": "Emissive", "DEP": "Depth", "DEPS": "DepthShadow",
          "DEPST": "DepthShadowTetrahedron", "VEL": "Velocity", "BAKE": "Bake"}
PASS_IDS = {p: i for i, p in enumerate(["DEF", "FOR", "EMI", "DEP", "DEPS", "DEPST", "VEL", "BAKE"])}
PASS_ENGINE = 255
INSTANCING = {"ST": "Static", "STI": "StaticInstanced"}
VARIANTS = [i + p for i in ("Static", "StaticInstanced") for p in PASSES.values()]
VARIANT_IDS = {v: i for i, v in enumerate(VARIANTS)}

_BSHD_RE = re.compile(r"^(?P<dir>.+/Assets/Materials/(?:.+/)?)(?P<stem>[^/]+)_(?P<inst>STI|ST)_(?P<pass>DEF|FOR|EMI|DEPST|DEPS|DEP|VEL|BAKE)_Vulkan\.bshd$")

# ---- class flags (bit per property; the primary class is a separate enum) ----
F_OPAQUE, F_ALPHA_TEST, F_ALPHA_BLEND, F_ADDITIVE = 1 << 0, 1 << 1, 1 << 2, 1 << 3
F_EMISSIVE, F_REFRACTIVE, F_SSS, F_HAIR = 1 << 4, 1 << 5, 1 << 6, 1 << 7
F_FOLIAGE, F_CLOTH, F_ANISOTROPIC, F_UNLIT = 1 << 8, 1 << 9, 1 << 10, 1 << 11
F_WATER, F_DECAL, F_VIRTUAL_TEXTURE, F_TWO_SIDED = 1 << 12, 1 << 13, 1 << 14, 1 << 15
F_WPO, F_VERTEX_COLOR, F_LUMINANCE_EMISSIVE, F_CHARACTER = 1 << 16, 1 << 17, 1 << 18, 1 << 19
F_VFX, F_FORWARD_LIT, F_SCENE_COLOR, F_DISCARDS = 1 << 20, 1 << 21, 1 << 22, 1 << 23
FLAG_NAMES = ["opaque", "alpha_test", "alpha_blend", "additive", "emissive", "refractive", "sss", "hair",
              "foliage", "cloth", "anisotropic", "unlit", "water", "decal", "virtual_texture", "two_sided",
              "world_position_offset", "vertex_color", "luminance_emissive", "character", "vfx", "forward_lit",
              "scene_color", "discards"]

CLASSES = ["opaque", "alpha_test", "emissive", "refractive", "sss_skin", "hair", "foliage", "transparent",
           "unlit", "cloth", "anisotropic", "water", "decal"]
CLASS_IDS = {c: i for i, c in enumerate(CLASSES)}

# ---- parameter roles ----
ROLES = ["other", "base_color_map", "normal_map", "physical_map", "emissive_map", "mask_map", "base_color",
         "emissive_color", "emissive_intensity", "roughness", "metalness", "reflectance", "alpha_cutoff",
         "opacity", "vt_table", "vt_albedo", "vt_normal", "vt_physical", "vt_tileset_index", "vt_uv_transform",
         "diffusion_profile_index", "light_channel", "fade_opacity", "detail_normal_map", "sampler"]
ROLE_IDS = {r: i for i, r in enumerate(ROLES)}

KINDS = ["none", "scalar", "vec2", "vec3", "vec4", "texture", "sampler", "vt_table", "vt_cache", "engine_cb"]
KIND_IDS = {k: i for i, k in enumerate(KINDS)}

_TEX_RULES = [  # first match wins
    ("detail_normal_map", re.compile(r"(detail|micro|wrinkle|iris|flow|erosion|foam).*(normal|nm)|(normal|nm).*(detail|mask|msk)", re.I)),
    ("normal_map", re.compile(r"(^|_)(normal|normalmap|nm|nm_map)($|_)|normal_?map", re.I)),
    ("emissive_map", re.compile(r"glow|emiss", re.I)),
    ("base_color_map", re.compile(r"(^|_)(base_?colou?r|basemap|bm|bm_map|albedo|diffuse|basemap_rgba|basemap_a)($|_)|^base_?colou?r", re.I)),
    ("physical_map", re.compile(r"(^|_)(physical|physicalmap|pm|pm_map|roughness|metal\w*)($|_)|physical_?map", re.I)),
    ("mask_map", re.compile(r"msk|mask", re.I)),
]
_SCALAR_RULES = [
    ("alpha_cutoff", re.compile(r"^(cliptest|alphaclip|alpha_?clip|alpha_?cut\w*|cutoff|alphatest\w*|alpha_?threshold)$", re.I)),
    ("emissive_intensity", re.compile(r"^(glow_?multiplier|glow_?intensity|emissive(_?(intensity|mult|multiplier|power|strength))?|emissivemult|glow_?power)$", re.I)),
    ("roughness", re.compile(r"^roughness(_?base)?$", re.I)),
    ("metalness", re.compile(r"^metal(ness|lic)?$", re.I)),
    ("reflectance", re.compile(r"^reflectance(_?base)?$", re.I)),
    ("opacity", re.compile(r"^opacity(_?mult)?$", re.I)),
]
_VECTOR_RULES = [
    ("emissive_color", re.compile(r"^(glow_?colou?r|emissive_?colou?r|emissive|eyes_glowcolour)$", re.I)),
    ("base_color", re.compile(r"^(base_?colou?r|colou?r|tint_?colou?r|basecolor)$", re.I)),
]
_EMISSIVE_NAME = re.compile(r"emissive|glow|lava|_gm_?pulse|pulse_?gm", re.I)
_ALPHA_TEST_NAME = re.compile(r"alpha_?test|alphatested|_at(_|$)|dither", re.I)
_FOLIAGE_NAME = re.compile(r"wind_(bushes|grass|vertex)|grass|foliage|leaves|leaf|bush|tree|ivy|vegetation", re.I)
_TWO_SIDED = re.compile(r"(^|_)2s(_|$)", re.I)


@dataclass
class Param:
    role: str
    kind: str
    set: int = 255
    binding: int = 255
    offset: int = 0xFFFF
    layer: int = 255         # vt cache layer (0 albedo, 1 normal, 2 physical)
    name: str = ""
    default: tuple = (0.0, 0.0, 0.0, 0.0)

    def key(self):
        return (self.role, self.kind, self.set, self.binding, self.offset, self.layer, self.name, self.default)


@dataclass
class Variant:
    template: int
    variant: int
    engine_cb: tuple = (255, 255, 0)     # set, binding, size
    material_cb: tuple = (255, 255, 0)
    params: list = field(default_factory=list)


@dataclass
class Template:
    name: str            # path below Assets/Materials, without extension
    path: str
    flags: int
    cls: str
    shading_model: int
    blend_state: int
    material_type: int
    material_usage: int
    raster_state: int
    depth_state: int
    uv_count: int
    diffusion_profile: str
    attrs: dict


@dataclass
class ShaderEntry:
    hash: int
    stage: str
    pass_id: int
    refs: list           # (template index, variant index) of every bshd that contains it


@dataclass
class MaterialDB:
    templates: list
    variants: list       # Variant
    shaders: list        # ShaderEntry (materials)
    engine: list         # (hash, name, stage)
    stats: dict


def _binding(node):
    if node is None:
        return None
    s, b = node.get("VkDescriptorSet", -1), node.get("VkBindingIndex", -1)
    if s is None or b is None or s < 0 or b < 0:
        return None
    return int(s), int(b)


def _classify(name: str, rules) -> str:
    for role, rx in rules:
        if rx.search(name or ""):
            return role
    return "other"


def _default(node, comps):
    v = node.get("Value", node.get("BaseValue"))
    if v is None:
        return (0.0,) * 4
    if not isinstance(v, (list, tuple)):
        v = [v]
    v = [float(x) for x in v][:4]
    return tuple(v + [0.0] * (4 - len(v)))


def parse_template(path: str, data: bytes):
    res = lsf.parse(data)
    root = res.region("Material") or (res.regions[0] if res.regions else None)
    if root is None:
        return None
    a = dict(root.attrs)
    shading = int(a.get("ShadingModel", 0) or 0)
    blend = int(a.get("BlendStateID", 7) or 0)
    name = path.split("/Assets/Materials/", 1)[-1].rsplit(".", 1)[0]
    stem = os.path.basename(name)

    # per-variant engine/material constant buffers and engine parameter offsets
    sdesc = {}
    for c in root.children:
        if c.name != "ShaderDescriptions":
            continue
        for sd in c.children:
            var = sd.name.replace("ShaderDesc_", "")
            sdesc[var] = sd
    params_by_var = {v: [] for v in VARIANTS}
    cbs = {}
    for var, sd in sdesc.items():
        if var not in VARIANT_IDS:
            continue
        ecb = _binding(sd.child("EngineCBBinding"))
        mcb = _binding(sd.child("MaterialCBSize"))
        cbs[var] = ((ecb or (255, 255)) + (int(sd.get("EngineCBSize", 0) or 0),),
                    (mcb or (255, 255)) + (int(sd.get("MaterialCBSize", 0) or 0),))
        if ecb:
            for attr, role in (("VirtualTexture_TilesetDataIndex", "vt_tileset_index"),
                               ("VirtualTexture_CB_Texture", "vt_uv_transform"),
                               ("DiffusionProfileIndex", "diffusion_profile_index"),
                               ("LightChannel", "light_channel"), ("FadeOpacity", "fade_opacity")):
                if attr in sd.attrs:
                    params_by_var[var].append(Param(role, "engine_cb", ecb[0], ecb[1], int(sd.get(attr)), name=attr))

    have_vt = have_emissive_param = has_cut = False
    for c in root.children:
        kind = {"ScalarParameters": "scalar", "Vector2Parameters": "vec2", "Vector3Parameters": "vec3",
                "VectorParameters": "vec4", "Texture2DParameters": "texture", "SamplerStateParameters": "sampler",
                "VirtualTextureParameters": "vt"}.get(c.name)
        if kind is None:
            continue
        pname = c.get("ParameterName") or ""
        if kind in ("scalar", "vec2", "vec3", "vec4"):
            comps = {"scalar": 1, "vec2": 2, "vec3": 3, "vec4": 4}[kind]
            role = _classify(pname, _SCALAR_RULES if kind == "scalar" else _VECTOR_RULES if kind in ("vec3", "vec4") else [])
            if role in ("emissive_color", "emissive_intensity"):
                have_emissive_param = True
            if role == "alpha_cutoff":
                has_cut = True
            dflt = _default(c, comps)
            if role == "other":
                continue  # constants without a known role (Blood, Dirt, ...) are not kept
            for var in VARIANTS:
                off = c.get(var)
                if off is None or var not in cbs or cbs[var][1][0] == 255:
                    continue
                s, b, _ = cbs[var][1]
                params_by_var[var].append(Param(role, kind, s, b, int(off), name=pname, default=dflt))
        elif kind == "sampler":
            continue  # samplers are fixed per binding and not needed to read a material
        elif kind == "texture":
            role = _classify(pname, _TEX_RULES)
            if role == "emissive_map":
                have_emissive_param = True
            for vn in c.children:
                if vn.name not in VARIANT_IDS:
                    continue
                sb = _binding(vn)
                if sb:
                    params_by_var[vn.name].append(Param(role, kind, sb[0], sb[1], name=pname))
        else:  # virtual texture: translation table + three cache layers
            have_vt = True
            for vn in c.children:
                if vn.name not in VARIANT_IDS:
                    continue
                for slot in vn.children:
                    sb = _binding(slot)
                    if not sb:
                        continue
                    if slot.name == "TranslationTable":
                        params_by_var[vn.name].append(Param("vt_table", "vt_table", sb[0], sb[1], name=pname))
                    elif slot.name.startswith("Cache") and slot.name[5:].isdigit():
                        layer = int(slot.name[5:])
                        role = ("vt_albedo", "vt_normal", "vt_physical")[layer] if layer < 3 else "other"
                        params_by_var[vn.name].append(Param(role, "vt_cache", sb[0], sb[1], layer=layer, name=pname))

    flags = 0
    if blend == 7:
        flags |= F_OPAQUE
    elif blend == 1:
        flags |= F_ADDITIVE
    else:
        flags |= F_ALPHA_BLEND
    if _ALPHA_TEST_NAME.search(stem) or has_cut:
        flags |= F_ALPHA_TEST
    if have_emissive_param or a.get("IsLuminanceEmissive") or _EMISSIVE_NAME.search(stem):
        flags |= F_EMISSIVE
    if a.get("IsLuminanceEmissive"):
        flags |= F_LUMINANCE_EMISSIVE
    if a.get("IsRefracting"):
        flags |= F_REFRACTIVE
    if shading == 3 or a.get("DiffusionProfileUUID"):
        flags |= F_SSS
    if shading == 5:
        flags |= F_HAIR
    if shading == 4 or _FOLIAGE_NAME.search(stem):
        flags |= F_FOLIAGE
    if shading == 6:
        flags |= F_CLOTH
    if shading == 2:
        flags |= F_ANISOTROPIC
    if shading == 1:
        flags |= F_UNLIT
    if int(a.get("MaterialType", 0) or 0) in (8, 17) or stem.upper().startswith("FLUID_WATER"):
        flags |= F_WATER
    if int(a.get("MaterialUsage", 0) or 0) == 1 or "decal" in stem.lower():
        flags |= F_DECAL
    if have_vt:
        flags |= F_VIRTUAL_TEXTURE
    if _TWO_SIDED.search(stem) or int(a.get("RasterizerStateID", 1) or 0) == 0:
        flags |= F_TWO_SIDED
    if a.get("IsUsingWorldPositionOffset"):
        flags |= F_WPO
    if a.get("VertexColorEnabled"):
        flags |= F_VERTEX_COLOR
    if stem.upper().startswith("CHAR_"):
        flags |= F_CHARACTER
    if stem.upper().startswith("VFX_"):
        flags |= F_VFX
    if int(a.get("ForwardLightingMode", 0) or 0) == 2:
        flags |= F_FORWARD_LIT
    if a.get("SceneColorEnabled"):
        flags |= F_SCENE_COLOR
    if a.get("IsDiscardingPixels"):
        flags |= F_DISCARDS

    # primary class, most specific first
    if flags & F_REFRACTIVE:
        cls = "refractive"
    elif flags & F_WATER:
        cls = "water"
    elif flags & F_DECAL:
        cls = "decal"
    elif flags & F_HAIR:
        cls = "hair"
    elif shading == 3:
        cls = "sss_skin"
    elif shading == 4 or (flags & F_FOLIAGE and flags & F_ALPHA_TEST):
        cls = "foliage"
    elif flags & F_UNLIT and flags & F_EMISSIVE:
        cls = "emissive"
    elif flags & (F_ALPHA_BLEND | F_ADDITIVE):
        cls = "transparent"
    elif flags & F_EMISSIVE and have_emissive_param:
        cls = "emissive"
    elif flags & F_ALPHA_TEST:
        cls = "alpha_test"
    elif flags & F_CLOTH:
        cls = "cloth"
    elif flags & F_ANISOTROPIC:
        cls = "anisotropic"
    elif flags & F_UNLIT:
        cls = "unlit"
    else:
        cls = "opaque"

    keep = {k: v for k, v in a.items() if not isinstance(v, (bytes, dict))}
    tpl = Template(name, path, flags, cls, shading, blend, int(a.get("MaterialType", 0) or 0),
                   int(a.get("MaterialUsage", 0) or 0), int(a.get("RasterizerStateID", 0) or 0),
                   int(a.get("DepthStateID", 0) or 0), int(a.get("UVCount", 0) or 0),
                   str(a.get("DiffusionProfileUUID") or ""), keep)
    return tpl, cbs, params_by_var


# ---- worker processes: hashing is pure Python, so it is spread over cores ----
_WORKER_PS = None


def _hash_worker(args):
    data_dir, paks, names = args
    global _WORKER_PS
    if _WORKER_PS is None:
        _WORKER_PS = PackageSet(data_dir, only=paks)
    out = []
    for n in names:
        try:
            for m in spirv.modules(_WORKER_PS.read(n)):
                out.append((n, m.stage, m.hash))
        except Exception as e:  # a broken file must not stop the build
            out.append((n, "error:" + str(e), 0))
    return out


def hash_bshd(data_dir: str, paks: list, names: list, jobs: int):
    if not names:
        return []
    if jobs <= 1:
        return _hash_worker((data_dir, paks, names))
    chunks = [names[i::jobs * 4] for i in range(jobs * 4)]
    res = []
    with ProcessPoolExecutor(jobs) as ex:
        for r in ex.map(_hash_worker, [(data_dir, paks, c) for c in chunks]):
            res.extend(r)
    return res


def extract(ps: PackageSet, data_dir: str, jobs: int = 0, log=print) -> MaterialDB:
    jobs = jobs or min(16, os.cpu_count() or 1)
    mat_bshd = {}
    engine_bshd = []
    for n in ps.index:
        if not n.endswith(".bshd"):
            continue
        m = _BSHD_RE.match(n)
        if m:
            mat_bshd[n] = m
        elif n.startswith("Shaders/Vulkan/"):
            engine_bshd.append(n)
    # templates
    tpl_paths = sorted({m.group("dir") + m.group("stem") + ".lsf" for m in mat_bshd.values()})
    templates, tindex, tparams, tcbs = [], {}, {}, {}
    missing = 0
    for p in tpl_paths:
        if p not in ps:
            missing += 1
            continue
        parsed = parse_template(p, ps.read(p))
        if not parsed:
            missing += 1
            continue
        tpl, cbs, pbv = parsed
        tindex[p] = len(templates)
        tparams[p], tcbs[p] = pbv, cbs
        templates.append(tpl)
    log(f"  materials: {len(templates)} templates ({missing} bshd stems without a template), "
        f"{len(mat_bshd)} material bshd, {len(engine_bshd)} engine bshd")

    owners = sorted({ps.owner(n) for n in list(mat_bshd) + engine_bshd})
    hashed = hash_bshd(data_dir, owners, sorted(mat_bshd) + sorted(engine_bshd), jobs)
    errors = [h for h in hashed if h[1].startswith("error:")]

    variants, vindex = [], {}
    shaders: dict[int, ShaderEntry] = {}
    engine: dict[int, tuple] = {}
    for n, stage, h in hashed:
        if stage.startswith("error:"):
            continue
        m = mat_bshd.get(n)
        if m is None:
            nm = n[len("Shaders/Vulkan/"):].rsplit(".", 1)[0]
            engine.setdefault(h, (h, nm, stage))
            continue
        tp = m.group("dir") + m.group("stem") + ".lsf"
        if tp not in tindex:
            continue
        var = INSTANCING[m.group("inst")] + PASSES[m.group("pass")]
        key = (tindex[tp], var)
        if key not in vindex:
            ecb, mcb = tcbs[tp].get(var, ((255, 255, 0), (255, 255, 0)))
            vindex[key] = len(variants)
            variants.append(Variant(tindex[tp], VARIANT_IDS[var], ecb, mcb, tparams[tp].get(var, [])))
        e = shaders.get(h)
        if e is None:
            e = shaders[h] = ShaderEntry(h, stage, PASS_IDS[m.group("pass")], [])
        e.refs.append(vindex[key])
    # Canonical reference first: the least specific template (fewest class flags, then the
    # shortest name). The deferred FS of Base_VT is also Base_AlphaTest_VT's (the cut-out is
    # done in the depth pass), and it should read as Base_VT.
    def rank(vi):
        t = templates[variants[vi].template]
        return (bin(t.flags).count("1"), len(t.name), t.name, variants[vi].variant)
    for e in shaders.values():
        e.refs = sorted(set(e.refs), key=rank)
    stats = {"templates": len(templates), "variants": len(variants), "material_shaders": len(shaders),
             "engine_shaders": len(engine), "bshd_errors": len(errors),
             "shared_hashes": sum(1 for e in shaders.values() if len(e.refs) > 1),
             "classes": {c: sum(1 for t in templates if t.cls == c) for c in CLASSES}}
    log(f"  materials: {len(shaders)} material shader hashes ({stats['shared_hashes']} shared by several "
        f"templates), {len(engine)} engine shader hashes, {len(errors)} unreadable bshd")
    return MaterialDB(templates, variants, sorted(shaders.values(), key=lambda e: e.hash),
                      sorted(engine.values()), stats)
