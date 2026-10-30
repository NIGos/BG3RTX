"""Place ONLY the previously exported board through its actual foundry hierarchy.

No new mesh/texture export, game, GPU or dynamic-state/scene-closure claims.
"""
import json
import hashlib
import math
from decimal import Decimal,localcontext
from pathlib import Path
from . import lsf
from .affine import Affine,UnsupportedAffine,quaternion_precision
from .build import DEFAULT_GAME,find_data_dir
from .lspk import PackageSet
from .extract.lights import TemplateResolver

ROOT=Path(__file__).resolve().parents[2]


def decimal_point(point,chain):
    """Independent high-precision Hamilton q*p*q^-1, not matrix reuse."""
    def mul(a,b):
        x,y,z,w=a;i,j,k,s=b
        return (w*i+x*s+y*k-z*j,w*j-x*k+y*s+z*i,w*k+x*j-y*i+z*s,w*s-x*i-y*j-z*k)
    with localcontext() as ctx:
        ctx.prec=70
        p=tuple(Decimal.from_float(float(x)) for x in point)
        for node in reversed(chain):
            q=tuple(Decimal.from_float(float(x)) for x in node.get('RotationQuat',(0,0,0,1)))
            length=sum(x*x for x in q).sqrt();q=tuple(x/length for x in q)
            s=node.get('Scale',1);s=s if isinstance(s,(list,tuple)) else [s]*3
            v=tuple(x*Decimal.from_float(float(y)) for x,y in zip(p,s))+(Decimal(0),)
            v=mul(mul(q,v),(-q[0],-q[1],-q[2],q[3]))
            p=tuple(v[i]+Decimal.from_float(float(node.get('Position',(0,0,0))[i])) for i in range(3))
        return tuple(float(x) for x in p)


def template_chain(resolver,key):
    chain=list(resolver.chain(key))
    if not chain or chain[-1].get('ParentTemplateId'):raise UnsupportedAffine('Incomplete/cyclic native template inheritance')
    if any(n.child('Transform') for n in chain):
        raise UnsupportedAffine('Template spatial override semantics outside this selected placement')
    return chain


def transform_mesh(mesh,affine):
    if set(mesh['attributes'])-{'POSITION','NORMAL','TANGENT','TEXCOORD_0'}:
        raise UnsupportedAffine('Unreviewed skin/morph/extra vertex deformation')
    out=dict(mesh);out['attributes']=dict(mesh['attributes'])
    out['attributes']['POSITION']=[affine.point(p) for p in mesh['attributes']['POSITION']]
    frames=[affine.tangent_frame(n,t) for n,t in zip(mesh['attributes']['NORMAL'],mesh['attributes']['TANGENT'])]
    out['attributes']['NORMAL']=[v[0] for v in frames];out['attributes']['TANGENT']=[v[1] for v in frames]
    out['indices']=list(mesh['indices'])
    if affine.determinant()<0:
        for i in range(0,len(out['indices']),3):out['indices'][i+1],out['indices'][i+2]=out['indices'][i+2],out['indices'][i+1]
    out['bounds']=[[f(p[j] for p in out['attributes']['POSITION']) for j in range(3)] for f in (min,max)]
    return out


def main():
    out=ROOT/'captures/m2-foundry';evidence=json.loads((out/'evidence.json').read_text())
    meshes=json.loads((out/'mesh-decoded.json').read_text())
    gltf=json.loads((out/'mesh.gltf').read_text())
    for name in ('mesh-decoded.json','mesh.gltf'):
        if hashlib.sha256((out/name).read_bytes()).hexdigest()!=evidence['output_fingerprints'][name]:
            raise UnsupportedAffine('Verified export changed; do not mix placement and geometry snapshots')
    if gltf.get('skins') or gltf.get('animations') or any(set(n)-{'mesh'} for n in gltf['nodes']):
        raise UnsupportedAffine('Unreviewed exported node pose/animation')
    if any(p.get('targets') for m in gltf['meshes'] for p in m['primitives']):raise UnsupportedAffine('Morph deformation')
    for m in gltf['meshes']:
        flags=m['extensions']['EXT_lslib_profile']
        if any(flags.get(k) for k in ('Cloth','Spring','ClothPhysics','Cloth01','Cloth02','Cloth04','MeshProxy','ProxyGeometry','Impostor','Occluder')):
            raise UnsupportedAffine('Unsupported deformed/proxy mesh')
    if any(evidence['visual'].get(k) for k in ('SkeletonResource','AttachmentSkeletonResource','SoftbodyResourceID','ClothColliderResourceID')):
        raise UnsupportedAffine('Native skeleton/softbody/cloth association')
    ps=PackageSet(find_data_dir(DEFAULT_GAME));resolver=TemplateResolver(ps,types=('scenery','LevelTemplate'))
    paths=[];steps=[];templates=[]
    for parent,child in (('CTY_Main_A','CTY_LowerCity_A'),('CTY_LowerCity_A','BGH_SteelWatchFoundry_B')):
        candidates=[]
        for path in ps.index:
            if '/Levels/'+parent+'/LevelTemplates/_merged.lsf' in path:
                raw=ps.read(path)
                for n in lsf.parse(raw).find_all('GameObjects'):
                    chain=list(resolver.chain(n.get('TemplateName','')))
                    sub=next((t.get('SubLevelName') for t in chain if t.get('SubLevelName')),None)
                    if sub==child:candidates.append((path,raw,n,template_chain(resolver,n.get('TemplateName',''))))
        if len(candidates)!=1:raise UnsupportedAffine('Ambiguous/missing selected level placement')
        path,raw,node,chain=candidates[0]
        paths.append(dict(path=path,sha256=hashlib.sha256(raw).hexdigest(),placement=node.to_dict()))
        steps.append(node.child('Transform'));templates.extend(n.to_dict() for n in chain)
    placements=evidence['foundry_placements_raw']
    if len(placements)!=1:raise UnsupportedAffine('Selected probe expects one authored board instance')
    entity=placements[0];chain=template_chain(resolver,entity['TemplateName']);templates.extend(n.to_dict() for n in chain)
    entity_path='Mods/GustavDev/Levels/BGH_SteelWatchFoundry_B/Scenery/_merged.lsf'
    entity_raw=ps.read(entity_path)
    current=[n for n in lsf.parse(entity_raw).find_all('GameObjects') if n.get('MapKey')==entity['MapKey']]
    if len(current)!=1 or current[0].to_dict()!=entity:raise UnsupportedAffine('Native placement changed after export')
    expected={n['MapKey']:n for n in templates};template_sources=[];seen=set()
    for path in ps.iter_suffix('RootTemplates/_merged.lsf'):
        raw=ps.read(path);matched=[]
        for node in lsf.parse(raw).find_all('GameObjects'):
            key=node.get('MapKey')
            if key in expected:
                if node.to_dict()!=expected[key]:raise UnsupportedAffine('Conflicting native template definition')
                matched.append(key);seen.add(key)
        if matched:template_sources.append(dict(path=path,sha256=hashlib.sha256(raw).hexdigest(),template_ids=matched))
    if seen!=expected.keys():raise UnsupportedAffine('Template provenance is incomplete')
    entity_transform=next(c for c in entity['children'] if c['_']=='Transform')
    steps.append(entity_transform)
    affine=Affine();raw_affine=Affine()
    for step in steps:
        affine=affine.compose(Affine.from_node(step))
        raw_affine=raw_affine.compose(Affine.from_node(step,normalize_quaternion=False))
    rows=[]
    for mesh in meshes:
        world=transform_mesh(mesh,affine)
        errors=[];bounds=[];raw_points=[];normalization_delta=[]
        for p,w in zip(mesh['attributes']['POSITION'],world['attributes']['POSITION']):
            ref=decimal_point(p,steps);error=max(abs(a-b) for a,b in zip(w,ref));bound=affine.fp32_application_bound(p)
            if any(abs(a-b)>e for a,b,e in zip(w,ref,bound)):raise ValueError('World placement exceeds derived FP32 envelope')
            errors.append(error);bounds.append(max(bound))
            raw=raw_affine.point(p);raw_points.append(raw)
            normalization_delta.append(max(abs(a-b) for a,b in zip(raw,w)))
        filename=f'world-lod{mesh["lod"]}.json';(out/filename).write_text(json.dumps(world,separators=(',',':'))+'\n')
        rows.append(dict(lod=mesh['lod'],triangles=mesh['triangles'],bounds=world['bounds'],output=filename,
            max_decimal_reference_error=max(errors),max_declared_fp32_application_bound=max(bounds),
            raw_quaternion_matrix_bounds=[[f(p[j] for p in raw_points) for j in range(3)] for f in (min,max)],
            max_raw_vs_normalized_position_delta=max(normalization_delta)))
    result=dict(scope='ONE authored board placement in CTY_Main_A; LOD alternatives, never merged',
        active_lod=0,world_lod_alternatives=rows,linear=affine.linear,translation=affine.translation,determinant=affine.determinant(),
        level_sources=paths,template_inheritance=templates,source_entity=entity,
        root_template_sources=template_sources,entity_source=dict(path=entity_path,sha256=hashlib.sha256(entity_raw).hexdigest()),
        native_transform_steps=[n.to_dict() if hasattr(n,'to_dict') else n for n in steps],
        quaternion_precision=[quaternion_precision(n.get('RotationQuat',(0,0,0,1))) for n in steps],
        normalization_convention='Unit-normalized quaternions for baked geometry; raw coefficients in unit-quaternion matrix formula also evaluated; engine choice NOT proven',
        implementation_hashes={name:hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
            for name in ('affine.py','place_foundry_mesh.py')},
        math='full affine; inverse-transpose normalized normals; orthogonalized transformed tangents; mirrored winding/handedness',
        precision='Native F32 encoding plus declared12-op normalization envelope; FP64 bake/Hamilton70-digit reference; gamma7 FP32 application bound',
        limitations=['Authoring hierarchy, not live runtime state proof','No room closure, terrain, construction, HLOD or missing blockers',
            'Template spatial overrides, skin/morph/cloth/proxies explicitly unsupported','No native XFB/object bridge world-position comparison'],
        input_hashes={n:hashlib.sha256((out/n).read_bytes()).hexdigest() for n in ('evidence.json','mesh-decoded.json','mesh.gltf')})
    (out/'world-placement.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(world=rows,determinant=result['determinant'],translation=affine.translation),indent=2))

if __name__=='__main__':main()
