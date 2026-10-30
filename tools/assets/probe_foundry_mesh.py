"""ONE ordinary foundry placement, using existing archive/LSF and portable LSLib.

No game, saves, GPU, installation or scene exporter. Run from this worktree:
python -m tools.assets.probe_foundry_mesh --lslib <official Packed directory>
Outputs are private game data under captures/m2-foundry, never committed.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import xml.etree.ElementTree as ET
from .build import DEFAULT_GAME, find_data_dir
from .lspk import PackageSet
from . import lsf
from .extract.lights import TemplateResolver

BANK = 'Public/Shared/Content/Assets/Furniture/[PAK]_Hospital/_merged.lsf'
VISUAL = '006c6b34-2889-2eb0-1d2e-6bca8398d99a'
MATERIAL = 'cfacd158-d460-6810-78bf-6d7d2b0576f1'
ROOT = Path(__file__).resolve().parents[2]
COMPONENTS = {5121: ('B', 1), 5123: ('H', 2), 5125: ('I', 4), 5126: ('f', 4)}
WIDTH = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}


def accessor(doc, buffers, index):
    """Independent bounded glTF accessor read; no LSLib/SharpGLTF reuse."""
    a = doc['accessors'][index]
    if a.get('sparse') or a.get('normalized'):
        raise ValueError('Sparse/normalized accessor outside this probe')
    kind, size = COMPONENTS[a['componentType']]
    width = WIDTH[a['type']]
    view = doc['bufferViews'][a['bufferView']]
    raw = buffers[view['buffer']]
    base, length = view.get('byteOffset', 0), view['byteLength']
    offset, count = a.get('byteOffset', 0), a['count']
    stride = view.get('byteStride', width * size)
    if min(base, length, offset) < 0 or count <= 0 or stride < width * size or stride % size or (base + offset) % size:
        raise ValueError('Invalid accessor layout')
    if base + length > len(raw) or offset + (count - 1) * stride + width * size > length:
        raise ValueError('Accessor exceeds declared view/buffer')
    values = [struct.unpack_from('<' + kind * width, raw, base + offset + i * stride) for i in range(count)]
    if any(not math.isfinite(x) for row in values for x in row):
        raise ValueError('Nonfinite exported attributes')
    return values


def decode_mesh(path, visual):
    doc = json.loads(path.read_text())
    buffers = []
    for b in doc['buffers']:
        p = (path.parent / b['uri']).resolve()
        if not p.is_relative_to(path.parent.resolve()):
            raise ValueError('Nonlocal exported buffer')
        raw = p.read_bytes()
        if len(raw) != b['byteLength']:
            raise ValueError('Exported buffer byte length mismatch')
        buffers.append(raw)
    objects = {n.get('ObjectID'): n for n in visual.children_named('Objects')}
    meshes = []
    for m in doc['meshes']:
        profile = m['extensions']['EXT_lslib_profile']
        key = visual.get('Name') + '.' + m['name'] + '.' + str(profile['ExportOrder'])
        node = objects[key]
        if node.get('LOD') != profile['LOD']:
            raise ValueError('Native bank/export LOD mismatch')
        for primitive in m['primitives']:
            if primitive.get('mode', 4) != 4:
                raise ValueError('Non-triangle-list primitive')
            attrs = {k: accessor(doc, buffers, v) for k, v in primitive['attributes'].items()}
            if not {'POSITION', 'NORMAL', 'TANGENT', 'TEXCOORD_0'} <= attrs.keys():
                raise ValueError('Required native attributes absent')
            for name, width in {'POSITION':3,'NORMAL':3,'TANGENT':4,'TEXCOORD_0':2}.items():
                if any(len(row)!=width for row in attrs[name]):
                    raise ValueError('Unexpected attribute shape')
            n = len(attrs['POSITION'])
            indices = [r[0] for r in accessor(doc, buffers, primitive['indices'])]
            if any(len(v) != n for v in attrs.values()) or len(indices) % 3 or min(indices) < 0 or max(indices) >= n:
                raise ValueError('Invalid topology/attribute correspondence')
            bounds = [[f(p[j] for p in attrs['POSITION']) for j in range(3)] for f in (min, max)]
            uv_bounds=[[f(p[j] for p in attrs['TEXCOORD_0']) for j in range(2)] for f in (min,max)]
            meshes.append(dict(name=m['name'], lod=profile['LOD'], object_id=key,
                material_id=node.get('MaterialID'), exported_material=doc['materials'][primitive['material']]['name'],
                bounds=bounds, uv_bounds=uv_bounds, vertices=n, triangles=len(indices)//3, attributes=attrs, indices=indices))
    if {m['object_id'] for m in meshes}!=objects.keys():
        raise ValueError('Export omitted a native resource object/LOD')
    union = [[f(m['bounds'][side][j] for m in meshes) for j in range(3)] for side, f in enumerate((min, max))]
    # Independent native bank metadata, not the glTF accessor's own min/max.
    if union != [visual.get('BoundsMin'), visual.get('BoundsMax')]:
        raise ValueError(f'LOD union disagrees with native local bounds: {union}')
    return meshes


def check_collada(path, meshes):
    """Second exporter path: compare triangle positions at DAE's six decimals.

    Both exporters share GR2 parsing, so this is NOT an independent GR2 decoder.
    It checks glTF indexing against separately exported/deduplicated DAE streams.
    """
    ns = {'c': 'http://www.collada.org/2005/11/COLLADASchema'}
    root = ET.parse(path).getroot()
    geometries = {g.get('name'): g for g in root.findall('.//c:geometry', ns)}
    result = []
    for m in meshes:
        g = geometries[m['name']]
        sources = {}
        for source in g.findall('c:mesh/c:source', ns):
            a = source.find('c:technique_common/c:accessor', ns)
            data = list(map(float, source.find('c:float_array', ns).text.split()))
            stride = int(a.get('stride'))
            sources['#'+source.get('id')] = [tuple(data[i:i+stride]) for i in range(0, len(data), stride)]
        vertices = { '#'+v.get('id'): v.find('c:input[@semantic="POSITION"]', ns).get('source')
                    for v in g.findall('c:mesh/c:vertices', ns)}
        expanded = []
        for t in g.findall('c:mesh/c:triangles', ns):
            inputs = t.findall('c:input', ns)
            width = max(int(i.get('offset')) for i in inputs)+1
            vertex = next(i for i in inputs if i.get('semantic') == 'VERTEX')
            ix = list(map(int, t.find('c:p', ns).text.split()))
            # LSLib v1.20.4 emits direct position-source VERTEX references,
            # despite also writing a vertices wrapper. Check its numeric stream,
            # not general COLLADA schema conformance.
            ref = vertex.get('source')
            positions = sources[vertices.get(ref, ref)]
            expanded.extend(positions[ix[i+int(vertex.get('offset'))]] for i in range(0, len(ix), width))
        actual = [m['attributes']['POSITION'][i] for i in m['indices']]
        if len(expanded) != len(actual):
            raise ValueError('glTF/DAE triangle count differs')
        # F6 serialization contributes <= half a decimal unit, plus float64 parse.
        error = max(abs(a-b) for p, q in zip(expanded, actual) for a, b in zip(p, q))
        if error > 0.5e-6 + 4*math.ulp(4.0):
            raise ValueError(f'glTF/DAE ordered triangle positions disagree: {error}')
        result.append(dict(mesh=m['name'], ordered_corners=len(actual), max_position_error=error))
    return result


def main():
    ap = argparse.ArgumentParser(__doc__)
    ap.add_argument('--lslib', type=Path, required=True, help='Official portable release Packed directory')
    args = ap.parse_args()
    out = ROOT/'captures/m2-foundry'
    out.mkdir(parents=True, exist_ok=True)
    tools = args.lslib.resolve()/'Tools'
    pinned='5dfe246b93cbc95531e011dbf18478c665a9de4f922f8b4a2b73da5a865c76d0'
    if hashlib.sha256((tools/'LSLib.dll').read_bytes()).hexdigest()!=pinned:
        raise ValueError('This probe pins the official LSLib v1.20.4 binary')
    env = dict(os.environ, PATH=str(args.lslib.resolve())+os.pathsep+str(tools)+os.pathsep+os.environ.get('PATH',''))
    ps = PackageSet(find_data_dir(DEFAULT_GAME))
    folded = {n.casefold():n for n in ps.index}
    sources = []
    def extract(name, filename=None):
        raw = ps.read(name)
        if len(raw) > 256*1024**2: raise ValueError('Bounded asset read exceeded')
        sources.append(dict(path=name, package=ps.owner(name), bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest()))
        if filename: (out/filename).write_bytes(raw)
        return raw
    bank = lsf.parse(extract(BANK))
    visual = next(n for n in bank.region('VisualBank').children if n.get('ID')==VISUAL)
    material = next(n for n in bank.region('MaterialBank').children if n.get('ID')==MATERIAL)
    level='Mods/GustavDev/Levels/BGH_SteelWatchFoundry_B/Scenery/_merged.lsf'
    scenery=lsf.parse(extract(level))
    resolver=TemplateResolver(ps,types=('scenery',))
    placements=[]
    for node in scenery.find_all('GameObjects'):
        chain=[node]+list(resolver.chain(node.get('TemplateName','')))
        ref=next((n.get('VisualTemplate') for n in chain if n.get('VisualTemplate')),None)
        if ref==VISUAL:placements.append(node.to_dict())
    if not placements:raise ValueError('Selected visual not independently resolved in foundry scenery')
    mesh = folded[visual.get('SourceFile').replace('\\','/').casefold()]
    extract(mesh, 'mesh.gr2')
    extract(material.get('SourceFile'), 'material-template.lsf')
    for fmt in ('gltf','dae'):
        p = subprocess.run([str(tools/'Divine.exe'),'-g','bg3','-a','convert-model','-s',str(out/'mesh.gr2'),
                            '-d',str(out/f'mesh.{fmt}'),'-i','gr2','-o',fmt],env=env,capture_output=True,text=True)
        (out/f'export-{fmt}.log').write_text(p.stdout+p.stderr)
        p.check_returncode()
    meshes = decode_mesh(out/'mesh.gltf',visual)
    crosscheck = check_collada(out/'mesh.dae',meshes)
    vt = [n for n in material.children_named('VirtualTextureParameters') if n.get('Enabled')]
    if len(vt)!=1: raise ValueError('Selected probe requires exactly one enabled VT')
    texture = next(n for n in bank.region('VirtualTextureBank').children if n.get('ID')==vt[0].get('ID'))
    texname = texture.get('GTexFileName')
    matches = []
    # Scan only 16 GTS metadata files, never the 83 GB page-file collection.
    for name in ps.index:
        if name.lower().endswith('.gts'):
            raw = ps.read(name)
            if texname.encode('utf-16-le') in raw: matches.append(name)
    if len(matches)!=1: raise ValueError('Ambiguous or missing GTex name in installed GTS metadata')
    gts_name = Path(matches[0]).name
    extract(matches[0],gts_name)
    helper = Path(__file__).with_name('probe_foundry_vt.ps1')
    def vt_command(decode=False):
        return ['pwsh','-NoProfile','-File',str(helper),'-Tools',str(tools),'-Gts',str(out/gts_name),
                '-Texture',texname,'-Output',str(out/('albedo.dds' if decode else 'vt-plan.json'))]+(['-Decode'] if decode else [])
    subprocess.run(vt_command(),check=True,env=env)
    plan = json.loads((out/'vt-plan.json').read_text(encoding='utf-8-sig'))
    page_bytes = 0
    for page in plan['pages']:
        names=[n for n in ps.index if n.endswith('/'+page)]
        if len(names)!=1: raise ValueError('Ambiguous/missing page file')
        raw=extract(names[0],page);page_bytes+=len(raw)
        if page_bytes>128*1024**2:raise ValueError('Selected texture page budget exceeded')
    subprocess.run(vt_command(True),check=True,env=env)
    from PIL import Image
    image=Image.open(out/'albedo.dds');image.load()
    if image.size!=(plan['texture']['Width'],plan['texture']['Height']):raise ValueError('Texture extent mismatch')
    image.save(out/'albedo.png')
    (out/'mesh-decoded.json').write_text(json.dumps(meshes,separators=(',',':'))+'\n')
    summary = dict(scope='ONE ordinary foundry VisualBank resource; local asset only', visual=visual.to_dict(),
        foundry_placements_raw=placements,
        material=material.to_dict(),virtual_texture=texture.to_dict(),vt_plan=plan,
        meshes=[{k:v for k,v in m.items() if k not in ('attributes','indices')} for m in meshes],
        collada_crosscheck=crosscheck,texture_mode=image.mode,texture_extrema=image.getextrema(),
        source_fingerprints=sources, package_fingerprints=ps.fingerprint(),
        tool_version='LSLib v1.20.4',tool_source='https://github.com/Norbyte/lslib/releases/tag/v1.20.4',
        release_archive_sha256='5e02368fb8acafda9b45acba37a3f3bf507fc3d65a083a159abbeab06337190e',
        lslib_license_source='https://github.com/Norbyte/lslib/blob/v1.20.4/LICENSE',
        tool_fingerprints={str(p.relative_to(args.lslib.resolve())):hashlib.sha256(p.read_bytes()).hexdigest()
            for p in (tools/'LSLib.dll',tools/'Divine.exe',tools/'LSLibNative.dll',args.lslib.resolve()/'granny2.dll')},
        probe_source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        reader_source_sha256={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in
            ('tools/assets/lspk.py','tools/assets/lsf.py','tools/assets/resource.py','tools/assets/compress.py',
             'tools/assets/extract/lights.py','tools/assets/probe_foundry_vt.ps1')},
        base_git_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        output_fingerprints={name:hashlib.sha256((out/name).read_bytes()).hexdigest()
            for name in ('mesh.gltf','mesh.bin','mesh.dae','mesh-decoded.json','albedo.dds','albedo.png')},
        license='LSLib MIT; third-party bundled DLL licenses are separate; no binary redistribution',
        limitations=['No world placement/terrain/construction/HLOD/state reconstruction',
            'glTF Dummy material is NOT authored PBR: authoritative material/VT association is this sidecar',
            'DAE/glTF share the upstream GR2 parser; native bank bounds are independent',
            'LSLib DAE VERTEX direct-source links are read numerically, not COLLADA schema-certified',
            'VT level0 layer0 only; no native sampling/BRDF or scene closure claim',
            'PackageSet resolver precedence; loose overrides/mod completeness not certified'])
    (out/'evidence.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps(dict(output=str(out),meshes=summary['meshes'],texture=plan['texture']),indent=2))

if __name__=='__main__':main()
