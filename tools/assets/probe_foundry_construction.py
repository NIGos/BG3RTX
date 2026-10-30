"""Decode ONE of TWO explicit, undeformed foundry constructions (not a room).

Uses the already pinned portable LSLib; package reads and CPU exports only.
Generated meshes/JSON remain private in captures/m2-foundry/construction-96ed.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
from . import lsf
from .affine import Affine, UnsupportedAffine, vec, f32_ulp
from .build import DEFAULT_GAME, find_data_dir
from .lspk import PackageSet
from .place_foundry_mesh import transform_mesh
from .probe_foundry_mesh import decode_mesh, check_collada, accessor

ROOT=Path(__file__).resolve().parents[2]
SOURCE='Mods/GustavDev/Levels/BGH_SteelWatchFoundry_B/TileConstructions/_merged.lsf'
RECORD='96ed9025-f791-440b-b954-8f19c06bb543'
BANK='Public/SharedDev/Content/Assets/Tilesets/Generic/[PAK]_WALL_GEN_Glasshouse_Floor_C_2D_0n5H_A/_merged.lsf'
WALL_RECORD='569ba5ab-2ab9-4083-b44f-b2b42b337509'
WALL_BANK='Public/SharedDev/Content/Assets/Tilesets/Generic/[PAK]_WALL_GEN_Glasshouse_Mid_A_0n5D_4H_A/_merged.lsf'
NULL='00000000-0000-0000-0000-000000000000'


def tile_affine(tile, tiles):
    """Selected native Rotate is column-major; preserve its raw coefficients.

    This supports only explicit generated, unbent/unflipped unit-stretch tiles.
    Stretchable is a capability, not an applied factor; both point refs must be
    null and ScaleZ exactly one. No tile-spline/editor regeneration is attempted.
    """
    if tiles.get('ConstructionBend') is not False or tiles.get('ConstructionPlaceTwoTiles') is not False:
        raise UnsupportedAffine('Bend/two-tile construction is not represented')
    if tile.get('Flip') is not False or tile.get('ScaleZ')!=1 or tile.get('TwoSidedTileCount')!=0:
        raise UnsupportedAffine('Flip/stretch/two-sided authoring semantics not represented')
    if any(tile.get(k)!=NULL for k in ('Point1','Point2')):
        raise UnsupportedAffine('Linked spline deformation not represented')
    r=vec(tile.get('Rotate',()),9);p=vec(tile.get('Translate',()),3);s=tile.get('Scale')
    if not isinstance(s,(float,int)) or not math.isfinite(s) or s<=0:
        raise UnsupportedAffine('Unsupported tile scale')
    a=Affine(tuple(tuple(r[col*3+row]*s for col in range(3)) for row in range(3)),p)
    # Inverse-transpose validation rejects a singular/degenerate authored frame.
    a.normal((0,1,0))
    return a


def unique(nodes, key, value):
    matches=[n for n in nodes if n.get(key)==value]
    if len(matches)!=1:raise ValueError(f'Missing/ambiguous {key}: {value}')
    return matches[0]


def hierarchy(parent, ps):
    """Verify previously resolved level records, excluding the board transform."""
    steps=parent['native_transform_steps'][:2]
    if len(parent['level_sources'])!=2 or len(steps)!=2:raise ValueError('Level hierarchy incomplete')
    for source in parent['level_sources']+parent['root_template_sources']:
        if hashlib.sha256(ps.read(source['path'])).hexdigest()!=source['sha256']:
            raise ValueError('Level source changed since verified placement')
    a=Affine();raw=Affine()
    for step in steps:
        a=a.compose(Affine.from_node(step));raw=raw.compose(Affine.from_node(step,normalize_quaternion=False))
    return a,raw


def validate_export(doc, visual, buffers):
    # These four resources carry a dummy skeleton. Accept only literally
    # identity transforms/bind matrices and a single unit-weight identity joint.
    # Never treat arbitrary skinned resources as static by discarding their pose.
    if doc.get('animations') or any(set(n)-{'mesh','skin','name','children'} for n in doc['nodes']):
        raise UnsupportedAffine('Nonidentity node pose/animation not represented')
    identity=(1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.)
    for skin in doc.get('skins',[]):
        if any(j<0 or j>=len(doc['nodes']) for j in skin['joints']):raise ValueError('Invalid skin joint')
        matrices=accessor(doc,buffers,skin['inverseBindMatrices'])
        if len(matrices)!=len(skin['joints']) or any(m!=identity for m in matrices):
            raise UnsupportedAffine('Nonidentity inverse bind matrix')
    for i,m in enumerate(doc['meshes']):
        attached=[n for n in doc['nodes'] if n.get('mesh')==i]
        if len(attached)!=1:raise UnsupportedAffine('Missing/duplicated mesh node')
        if 'skin' in attached[0] and not 0<=attached[0]['skin']<len(doc.get('skins',[])):
            raise ValueError('Invalid mesh skin')
        for p in m['primitives']:
            attrs=p['attributes']
            if 'JOINTS_0' in attrs or 'WEIGHTS_0' in attrs:
                if 'skin' not in attached[0]:raise UnsupportedAffine('Skin streams without mesh skin')
                j=accessor(doc,buffers,attrs['JOINTS_0']);w=accessor(doc,buffers,attrs['WEIGHTS_0'])
                if any(v!=(0,0,0,0) for v in j) or any(v!=(1.,0.,0.,0.) for v in w):
                    raise UnsupportedAffine('Nontrivial vertex skinning')
    for m in doc['meshes']:
        if any(p.get('targets') for p in m['primitives']):raise UnsupportedAffine('Morph deformation')
        flags=m['extensions']['EXT_lslib_profile']
        if any(flags.get(k) for k in ('Cloth','Spring','ClothPhysics','Cloth01','Cloth02','Cloth04','MeshProxy','ProxyGeometry','Impostor','Occluder')):
            raise UnsupportedAffine('Deformed/proxy mesh not represented')
    if any(visual.get(k) for k in ('SkeletonResource','AttachmentSkeletonResource','SoftbodyResourceID','ClothColliderResourceID')):
        raise UnsupportedAffine('Native deforming visual association')


def span_evidence(record, instances):
    """Selected native endpoint identities discriminate layout and pivot use.

    This is an authoring consistency check, NOT native engine schema proof.
    Endpoint rounding allowance derives from stored FP32 fields, not observed
    gaps. Gaps are reported unchanged and are never repaired or snapped.
    """
    spline=record.child('ConstructionSpline')
    if spline is None:raise ValueError('Missing construction spline')
    points=[n for n in spline.children_named('ConstructionPoint') if n.get('ConstructionHelperPoint') is False]
    if len(points)!=2:raise UnsupportedAffine('Only the selected single straight segment is supported')
    endpoints=[vec(n.get('ConstructionPointTransform')[12:15],3) for n in points]
    if endpoints[0][:2]!=endpoints[1][:2]:raise UnsupportedAffine('Selected segment is not level-Z aligned')
    translations=[tuple(n['tile']['Translate']) for n in instances]
    if any(p not in translations for p in endpoints):raise ValueError('Native caps do not share exact spline endpoints')
    interior=[n for n in instances if n['tile']['Stretchable']]
    if len(interior)!=3:raise ValueError('Selected construction interior changed')
    lo,hi=sorted(p[2] for p in endpoints)
    # Independently expected local-X piece endpoints, not world AABB overlaps.
    segments=[];rounding=[]
    for n in interior:
        a=tile_affine(n['tile'],record.child('tiles'))
        xmin=min(m['bounds'][0][0] for m in n['meshes'] if m['lod']==0)
        xmax=max(m['bounds'][1][0] for m in n['meshes'] if m['lod']==0)
        ends=[a.point((x,0,0))[2] for x in (xmin,xmax)]
        rounding.extend(a.fp32_application_bound((x,0,0))[2]+abs(a.linear[2][0])*0.5*f32_ulp(x)
                        for x in (xmin,xmax))
        segments.append(dict(uuid=n['tile']['UUID'],z=sorted(ends)))
    segments.sort(key=lambda n:n['z'][0])
    # Declared stored-affine coefficient/application error, decoded source X
    # half-ULP propagated through the row, and target endpoint half-ULP. This is
    # an encoding/operation envelope, independent of the measured joint gaps.
    budget=max(rounding)+max(0.5*f32_ulp(lo),0.5*f32_ulp(hi))
    if abs(min(n['z'][0] for n in segments)-lo)>budget or abs(max(n['z'][1] for n in segments)-hi)>budget:
        raise ValueError('Rotate layout inconsistent with native endpoint segment')
    return dict(endpoints=endpoints,cap_translation_identity=True,interior_segments=segments,
        signed_joint_gaps=[b['z'][0]-a['z'][1] for a,b in zip(segments,segments[1:])],
        endpoint_rounding_envelope=budget,
        layout='Raw column-major Rotate9; explicit Translate is in level coordinates; construction pivot is not multiplied',
        status='AUTHORING_CONSISTENT_NOT_RUNTIME_QUALIFIED')


def wall_span_evidence(record,instances):
    """Native wall anchors and actual mesh intervals, without sealing overlaps.

    The decorative wall geometry deliberately extends past some tile anchors.
    Its bounds must NOT be treated as exact topological construction lengths.
    This records the source geometry rather than inventing a closure threshold.
    """
    spline=record.child('ConstructionSpline')
    points=[n for n in spline.children_named('ConstructionPoint') if n.get('ConstructionHelperPoint') is False]
    if len(points)!=2:raise UnsupportedAffine('Selected wall is no longer one straight segment')
    ends=[vec(n.get('ConstructionPointTransform')[12:15],3) for n in points]
    if ends[0][1:]!=ends[1][1:]:raise UnsupportedAffine('Selected wall is no longer level-X aligned')
    if any(p not in [tuple(n['tile']['Translate']) for n in instances] for p in ends):
        raise ValueError('Native wall endpoints no longer share explicit tile anchors')
    spans=[]
    for n in instances:
        a=tile_affine(n['tile'],record.child('tiles'))
        if a.vector((1,0,0))[0]>=0:raise ValueError('Native wall local +X must point toward decreasing level X')
        bounds=[m['bounds'] for m in n['meshes'] if m['lod']==0]
        xlo=min(b[0][0] for b in bounds);xhi=max(b[1][0] for b in bounds)
        spans.append(dict(uuid=n['tile']['UUID'],x=sorted(a.point((x,0,0))[0] for x in (xlo,xhi)),
            local_visual_x_bounds=[xlo,xhi],anchor=n['tile']['Translate']))
    spans.sort(key=lambda n:n['x'][0])
    return dict(endpoints=ends,endpoint_anchor_identity=True,actual_mesh_x_spans=spans,
        signed_neighbour_mesh_gaps=[b['x'][0]-a['x'][1] for a,b in zip(spans,spans[1:])],
        layout='Previously checked raw column-major Rotate9 and explicit level-coordinate Translate; construction pivot excluded',
        status='AUTHORING_CONSISTENT_NOT_RUNTIME_QUALIFIED',
        note='Mesh X intervals are not opening/solid coverage or watertightness proof; authored overlap is preserved')


def main():
    ap=argparse.ArgumentParser(__doc__);ap.add_argument('--lslib',type=Path,required=True)
    ap.add_argument('--construction',choices=('floor','wall'),default='floor');args=ap.parse_args()
    wall=args.construction=='wall';record_id=WALL_RECORD if wall else RECORD;bank_path=WALL_BANK if wall else BANK
    count=8 if wall else 5
    base=ROOT/'captures/m2-foundry';out=base/('construction-569b-wall' if wall else 'construction-96ed');out.mkdir(parents=True,exist_ok=True)
    packed=args.lslib.resolve();tools=packed/'Tools'
    if hashlib.sha256((tools/'LSLib.dll').read_bytes()).hexdigest()!='5dfe246b93cbc95531e011dbf18478c665a9de4f922f8b4a2b73da5a865c76d0':
        raise ValueError('Requires previously verified LSLib v1.20.4')
    env=dict(os.environ,PATH=str(packed)+os.pathsep+str(tools)+os.pathsep+os.environ.get('PATH',''))
    ps=PackageSet(find_data_dir(DEFAULT_GAME));sources=[]
    def read(path):
        raw=ps.read(path)
        if len(raw)>64*1024**2:raise ValueError('Selected construction read exceeds bound')
        sources.append(dict(path=path,bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest(),package=ps.owner(path)))
        return raw
    record=unique(lsf.parse(read(SOURCE)).find_all('GameObjects'),'MapKey',record_id)
    tiles=record.child('tiles');native_tiles=tiles.children_named('tile')
    if len(native_tiles)!=count or record.child('Fillings').children:raise ValueError('Selected explicit construction changed')
    bank=lsf.parse(read(bank_path));tile_set=unique(bank.region('TileSetBank').children,'ID',tiles.get('TileSet'))
    tile_map={n.get('TileGUID'):n for n in tile_set.find_all('Tile')}
    if len(tile_map)!=len(list(tile_set.find_all('Tile'))):raise ValueError('Duplicate native TileGUID')
    folded={n.casefold():n for n in ps.index};assets={}
    for tile in native_tiles:
        tile_affine(tile,tiles)
        ref=tile_map[tile.get('tile')];vid=ref.get('VisualGUID')
        if vid in assets:continue
        visual=unique(bank.region('VisualBank').children,'ID',vid)
        folder=out/vid;folder.mkdir(exist_ok=True)
        path=folded[visual.get('SourceFile').replace('\\','/').casefold()]
        (folder/'mesh.gr2').write_bytes(read(path))
        for fmt in ('gltf','dae'):
            result=subprocess.run([str(tools/'Divine.exe'),'-g','bg3','-a','convert-model','-s',str(folder/'mesh.gr2'),
                '-d',str(folder/f'mesh.{fmt}'),'-i','gr2','-o',fmt],env=env,capture_output=True,text=True)
            (folder/f'export-{fmt}.log').write_text(result.stdout+result.stderr);result.check_returncode()
        doc=json.loads((folder/'mesh.gltf').read_text())
        # decode_mesh independently validates local buffer URI/layout/length.
        meshes=decode_mesh(folder/'mesh.gltf',visual,allow_color_unorm8=True)
        buffers=[(folder/b['uri']).read_bytes() for b in doc['buffers']]
        validate_export(doc,visual,buffers)
        (folder/'mesh-decoded.json').write_text(json.dumps(meshes,separators=(',',':'))+'\n')
        assets[vid]=dict(visual=visual.to_dict(),meshes=meshes,crosscheck=check_collada(folder/'mesh.dae',meshes),
            fingerprints={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in folder.iterdir() if p.suffix in ('.gr2','.gltf','.bin','.dae','.json')})
    parent=json.loads((base/'world-placement.json').read_text());level,raw_level=hierarchy(parent,ps)
    instances=[];world_meshes=[];raw_delta=[];fp32_bounds=[]
    for tile in native_tiles:
        vid=tile_map[tile.get('tile')].get('VisualGUID');asset=assets[vid];local=tile_affine(tile,tiles)
        world=level.compose(local);raw=raw_level.compose(local)
        instance=dict(tile=tile.to_dict(),visual_id=vid,meshes=asset['meshes'],world_linear=world.linear,world_translation=world.translation)
        instances.append(instance)
        for mesh in asset['meshes']:
            if mesh['lod']!=0:continue
            # Identity skin was proved above. Preserve the original streams in
            # the local asset output and retain COLOR_0 unchanged in world data.
            prepared=dict(mesh,attributes={k:v for k,v in mesh['attributes'].items() if k not in ('COLOR_0','JOINTS_0','WEIGHTS_0')})
            transformed=transform_mesh(prepared,world)
            if 'COLOR_0' in mesh['attributes']:transformed['attributes']['COLOR_0']=mesh['attributes']['COLOR_0']
            transformed.update(tile_uuid=tile.get('UUID'),visual_id=vid,skin_policy='Verified literal identity bind pose only')
            world_meshes.append(transformed)
            for p,w in zip(mesh['attributes']['POSITION'],transformed['attributes']['POSITION']):
                raw_delta.append(max(abs(a-b) for a,b in zip(raw.point(p),w)))
                fp32_bounds.append(max(world.fp32_application_bound(p)))
    span=wall_span_evidence(record,instances) if wall else span_evidence(record,instances)
    geometry=out/'world-lod0.json';geometry.write_text(json.dumps(world_meshes,separators=(',',':'))+'\n')
    summary=dict(scope=f'ONE explicit foundry {args.construction} construction; {count} instances/{len(assets)} native resources; NOT a room or closure certificate',
        record=record.to_dict(),source_fingerprints=sources,assets={vid:{k:v for k,v in a.items() if k!='meshes'} for vid,a in assets.items()},
        active_lod=0,instances=[{k:v for k,v in n.items() if k!='meshes'} for n in instances],
        triangles=sum(m['triangles'] for m in world_meshes),vertices=sum(m['vertices'] for m in world_meshes),
        bounds=[[f(m['bounds'][side][j] for m in world_meshes) for j in range(3)] for side,f in enumerate((min,max))],
        native_material_ids=sorted({m['material_id'] for m in world_meshes}),
        span_evidence=span,level_sources=parent['level_sources'],root_template_sources=parent['root_template_sources'],
        level_transform_steps=parent['native_transform_steps'][:2],normalization_convention=parent['normalization_convention'],
        max_raw_vs_normalized_position_delta=max(raw_delta),max_declared_fp32_application_bound=max(fp32_bounds),
        output=dict(path=str(geometry),sha256=hashlib.sha256(geometry.read_bytes()).hexdigest()),
        parent_placement_sha256=hashlib.sha256((base/'world-placement.json').read_bytes()).hexdigest(),
        implementation_sha256={name:hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
            for name in ('probe_foundry_construction.py','probe_foundry_mesh.py','affine.py','place_foundry_mesh.py')},
        limitations=['No runtime/XFB transform or visibility agreement','Literal identity skin bind pose only; nonidentity skin/animation rejected',
            'Material identity retained, construction material textures/recipe not decoded here',
            'HLOD/dynamic visibility/destruction/fade state unresolved; no opacity inferred from editor Opacity field',
            'No bent/flipped/stretched/linked spline construction or regenerated tiles','No wall/roof/opening enclosure or room closure proof',
            'Raw tile matrices preserved; normalized level quaternion convention and raw alternative retained',
            'DAE/glTF share upstream GR2 parser; native resource bounds are independent'])
    (out/'evidence.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps({k:summary[k] for k in ('scope','triangles','vertices','bounds','native_material_ids','span_evidence','max_raw_vs_normalized_position_delta')},indent=2))


if __name__=='__main__':main()
