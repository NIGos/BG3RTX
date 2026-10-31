"""Decode ONE explicit bounded foundry selection (not a room).

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
SUPPORT_RECORD='090da40c-0519-44ce-a6cb-bff11a194484'
SUPPORT_BANK='Public/SharedDev/Content/Assets/Tilesets/Generic/[PAK]_WALL_GEN_Glasshouse_Floor_A_4D_0n5H_A/_merged.lsf'
SUPPORT_TILE='bfe78877-3d4b-b5c4-6522-2f7e83ae7de3'
NULL='00000000-0000-0000-0000-000000000000'
FLOOR_B_RECORD='2caca96a-a623-4bd6-81be-432788285a8b'
FLOOR_B_TILE='21cfceda-36af-b591-588c-93b7a32be058'
FLOOR_B_GUID='d9b38c0e-6755-4c05-9a4a-1e91aad64de5'
FLOOR_B_SET='2fdf2f54-687f-8894-4ac7-b7ffbe01bb22'
FLOOR_B_BANK='Public/SharedDev/Content/Assets/Tilesets/Generic/[PAK]_WALL_GEN_Glasshouse_Floor_B_2D_0n5H_A/_merged.lsf'


def tile_affine(tile, tiles, legacy_inactive=False, floor_b_position_only=False):
    """Selected native Rotate is column-major; preserve its raw coefficients.

    This supports only explicit generated, unbent/unflipped unit-stretch tiles.
    Stretchable is a capability, not an applied factor; both point refs must be
    null and ScaleZ exactly one. The explicit Floor_B POSITION opt-in below
    uses the native vertex oracle; it does not qualify UV/Flip texture semantics.
    No tile-spline/editor regeneration is attempted.
    """
    if tiles.get('ConstructionBend') is not False or tiles.get('ConstructionPlaceTwoTiles') is not False:
        raise UnsupportedAffine('Bend/two-tile construction is not represented')
    missing=object()
    missing_legacy=legacy_inactive and tile.get('Stretchable') is False and all(tile.get(k,missing) is missing for k in ('Point1','Point2','TwoSidedTileCount'))
    # Opt-in native POSITION evidence: S1 snapshot 20261031-022347, exact
    # Floor_B resource, unique board-anchored ordered vertex correspondence.
    # ScaleZ acts on local X. Flip does not modify observed POSITION; its UV
    # semantics are not certified here. Keep the default strict path unchanged.
    if floor_b_position_only:
        if tile.get('tile')!=FLOOR_B_GUID or tiles.get('TileSet')!=FLOOR_B_SET:
            raise UnsupportedAffine('Position-only Floor_B evidence does not cover this resource')
        if not isinstance(tile.get('Flip'),bool) or not isinstance(tile.get('Stretchable'),bool):
            raise UnsupportedAffine('Incomplete Floor_B flags')
        if tile.get('TwoSidedTileCount',0)!=0 or any(tile.get(k,NULL)!=NULL for k in ('Point1','Point2')):
            raise UnsupportedAffine('Linked/two-sided Floor_B deformation not represented')
        stretch=tile.get('ScaleZ')
        if not isinstance(stretch,(float,int)) or not math.isfinite(stretch) or stretch<=0:
            raise UnsupportedAffine('Invalid Floor_B local-X stretch')
    else:
        stretch=1.
    if not floor_b_position_only and (tile.get('Flip') is not False or tile.get('ScaleZ')!=1 or (tile.get('TwoSidedTileCount')!=0 and not missing_legacy)):
        raise UnsupportedAffine('Flip/stretch/two-sided authoring semantics not represented')
    if any(tile.get(k)!=NULL for k in ('Point1','Point2')) and not missing_legacy and not floor_b_position_only:
        raise UnsupportedAffine('Linked spline deformation not represented')
    r=vec(tile.get('Rotate',()),9);p=vec(tile.get('Translate',()),3);s=tile.get('Scale')
    if not isinstance(s,(float,int)) or not math.isfinite(s) or s<=0:
        raise UnsupportedAffine('Unsupported tile scale')
    a=Affine(tuple(tuple(r[col*3+row]*s*(stretch if col==0 else 1.) for col in range(3)) for row in range(3)),p)
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


def validate_export(doc, visual, buffers, allow_cancelled_translations=False):
    # These resources carry authoring skeletons. Accept only literally identity
    # weighted-joint ancestry/bind matrices and a single unit-weight joint.
    # Cell-only opt-in also proves exact cancellation of translation-only
    # ancestors with rational arithmetic; no nonidentity net pose is discarded.
    # Never treat arbitrary skinned resources as static by discarding their pose.
    if doc.get('animations'):raise UnsupportedAffine('Animated resource not represented')
    identity=(1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.)
    parents={}
    for i,n in enumerate(doc['nodes']):
        for child in n.get('children',[]):
            if child in parents or child<0 or child>=len(doc['nodes']):raise UnsupportedAffine('Invalid/shared node ancestry')
            parents[child]=i
    def require_identity_chain(index):
        from fractions import Fraction
        translation=[Fraction(0),Fraction(0),Fraction(0)]
        seen=set()
        while True:
            if index in seen:raise UnsupportedAffine('Cyclic weighted-joint ancestry')
            seen.add(index);n=doc['nodes'][index]
            allowed={'mesh','skin','name','children'}|({'translation'} if allow_cancelled_translations else set())
            if set(n)-allowed:raise UnsupportedAffine('Nonidentity used node pose')
            if 'translation' in n:
                t=vec(n['translation'],3)
                translation=[a+Fraction.from_float(v) for a,v in zip(translation,t)]
            if index not in parents:break
            index=parents[index]
        if any(translation):raise UnsupportedAffine('Nonidentity net node translation')
    for skin in doc.get('skins',[]):
        if any(j<0 or j>=len(doc['nodes']) for j in skin['joints']):raise ValueError('Invalid skin joint')
        matrices=accessor(doc,buffers,skin['inverseBindMatrices'])
        if len(matrices)!=len(skin['joints']) or not matrices or matrices[0]!=identity:
            raise UnsupportedAffine('Nonidentity weighted inverse bind matrix')
    for i,m in enumerate(doc['meshes']):
        attached=[n for n in doc['nodes'] if n.get('mesh')==i]
        if len(attached)!=1:raise UnsupportedAffine('Missing/duplicated mesh node')
        require_identity_chain(doc['nodes'].index(attached[0]))
        if 'skin' in attached[0] and not 0<=attached[0]['skin']<len(doc.get('skins',[])):
            raise ValueError('Invalid mesh skin')
        for p in m['primitives']:
            attrs=p['attributes']
            if 'JOINTS_0' in attrs or 'WEIGHTS_0' in attrs:
                if 'skin' not in attached[0]:raise UnsupportedAffine('Skin streams without mesh skin')
                j=accessor(doc,buffers,attrs['JOINTS_0']);w=accessor(doc,buffers,attrs['WEIGHTS_0'])
                if any(v!=(0,0,0,0) for v in j) or any(v!=(1.,0.,0.,0.) for v in w):
                    raise UnsupportedAffine('Nontrivial vertex skinning')
                skin=doc['skins'][attached[0]['skin']]
                require_identity_chain(skin['joints'][0])
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


def vertical_hits(meshes,x,z):
    """Actual triangle XZ intersection; near-edge roundoff is reported uncertain.

    No sampled/AABB coverage claim. Closed interval tests use a gamma32 FP64
    arithmetic enclosure; uncertain edge hits cannot certify geometric support.
    """
    hits=[];uncertain=0;u=2**-53;gamma=32*u/(1-32*u)
    for mesh in meshes:
        p=mesh['attributes']['POSITION'];ix=mesh['indices']
        for i in range(0,len(ix),3):
            a,b,c=(p[j] for j in ix[i:i+3]);bx,bz=b[0]-a[0],b[2]-a[2];cx,cz=c[0]-a[0],c[2]-a[2]
            px,pz=x-a[0],z-a[2];den=bx*cz-bz*cx
            scale=max(abs(v) for v in (bx,bz,cx,cz,px,pz))
            error=gamma*scale*scale
            if abs(den)<=error:
                # Degenerate projected triangles do not bound an XZ area.
                continue
            v=px*cz-pz*cx;w=bx*pz-bz*px
            if den<0:den,v,w=-den,-v,-w
            q=den-v-w
            if min(v,w,q)<-3*error:continue
            if min(v,w,q)<=3*error:uncertain+=1;continue
            height=a[1]+(v*(b[1]-a[1])+w*(c[1]-a[1]))/den
            hits.append(dict(height=height,tile_uuid=mesh.get('tile_uuid'),triangle=i//3,material_id=mesh['material_id']))
    return dict(interior_hits=hits,uncertain_projected_edge_candidates=uncertain)


def support_evidence(base,world_meshes):
    board=json.loads((base/'world-lod0.json').read_text())
    # This file has one ordinary asset/LOD in the verified export.
    positions=board['attributes']['POSITION'];minimum=min(p[1] for p in positions)
    parent=json.loads((base/'world-placement.json').read_text())
    envelope=2*parent['world_lod_alternatives'][0]['max_declared_fp32_application_bound']
    low=list(dict.fromkeys(tuple(p) for p in positions if p[1]<=minimum+envelope))
    samples=[]
    for p in low:
        sample=vertical_hits(world_meshes,p[0],p[2]);sample['board_position']=p
        for hit in sample['interior_hits']:hit['height_minus_board_vertex']=hit['height']-p[1]
        samples.append(sample)
    adjacent={}
    for name in ('construction-96ed','construction-569b-wall'):
        path=base/name/'evidence.json';bounds=json.loads(path.read_text())['bounds']
        adjacent[name]=dict(bounds=bounds,source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
            xz_axis_separation=[max(0.,board['bounds'][0][j]-bounds[1][j],bounds[0][j]-board['bounds'][1][j]) for j in (0,2)])
    return dict(board_source_sha256=hashlib.sha256((base/'world-lod0.json').read_bytes()).hexdigest(),
        board_bounds=board['bounds'],minimum_board_y=minimum,low_vertex_band=envelope,low_vertex_samples=samples,
        existing_adjacent_bounds=adjacent,
        strict_interior_hit_samples=sum(bool(s['interior_hits']) for s in samples),
        sample_count=len(samples),status='ACTUAL_TRIANGLES_AT_LOW_VERTICES_ONLY_NOT_FOOTPRINT_CLOSURE',
        note='No opacity/collision or physical contact certification; triangle heights relative to authored board are retained, including penetration/gaps')



def ground_candidate(bounds,board_bounds):
    """Broad phase only; projected triangle hits, not boxes, establish support."""
    return (bounds[0][1]<=board_bounds[0][1] and
            all(bounds[0][j]<=board_bounds[1][j] and bounds[1][j]>=board_bounds[0][j] for j in (0,2)))


def floor_b_ground_subset(base):
    """Export native-qualified nearby Floor_B plus one existing Floor_A corner.

    Exact captured LODs remain separate from the previous LOD0 alternatives.
    Only POSITION correspondence is qualified. Height crossings are retained;
    opacity, collision, UV and physical contact are not inferred.
    """
    import numpy as np
    from scripts.geo_view import load
    from .check_floor_b_positions import projection_bound
    oracle_path=base/'floor-b-native-oracle/native-position-oracle.json'
    oracle=json.loads(oracle_path.read_text())
    for name,sha in oracle['source_hashes'].items():
        if hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()!=sha:
            raise ValueError('Rerun native Floor_B oracle for current source before ground export')
    binary=Path(oracle['binary']['path'])
    if hashlib.sha256(binary.read_bytes()).hexdigest()!=oracle['binary']['sha256']:
        raise ValueError('Native snapshot changed')
    native=load(binary);cam=native['ubo'][52:55].astype(np.float64)
    board=json.loads((base/'world-lod0.json').read_text())
    ps=PackageSet(find_data_dir(DEFAULT_GAME));parent=json.loads((base/'world-placement.json').read_text())
    level,_=hierarchy(parent,ps)
    records={n.get('MapKey'):n for n in lsf.parse(ps.read(SOURCE)).find_all('GameObjects')}
    be=json.loads((base/'construction-floor-b-position/evidence.json').read_text())
    vid=be['instances'][0]['visual_id'];folder=base/'construction-floor-b-position'/vid
    for source in be['source_fingerprints']:
        if hashlib.sha256(ps.read(source['path'])).hexdigest()!=source['sha256']:raise ValueError('Native Floor_B source changed')
    for name,sha in be['assets'][vid]['fingerprints'].items():
        if hashlib.sha256((folder/name).read_bytes()).hexdigest()!=sha:raise ValueError('Floor_B local export changed')
    meshes=json.loads((folder/'mesh-decoded.json').read_text());selected=[];world=[]
    for row in oracle['native_matches']:
        record=records[row['record']];tiles=record.child('tiles')
        tile=unique(tiles.children_named('tile'),'UUID',row['tile_uuid'])
        affine=level.compose(tile_affine(tile,tiles,floor_b_position_only=True))
        mesh=next(m for m in meshes if m['lod']==row['match']['lod'])
        prepared=dict(mesh,attributes={k:v for k,v in mesh['attributes'].items() if k not in ('COLOR_0','JOINTS_0','WEIGHTS_0')})
        transformed=transform_mesh(prepared,affine)
        if not ground_candidate(transformed['bounds'],board['bounds']):continue
        transformed.update(tile_uuid=row['tile_uuid'],visual_id=vid,native_draw=row['match']['draw'],native_instance=row['match']['instance'])
        if 'COLOR_0' in mesh['attributes']:transformed['attributes']['COLOR_0']=mesh['attributes']['COLOR_0']
        world.append(transformed);selected.append(dict(record=row['record'],tile_uuid=row['tile_uuid'],native=row['match'],bounds=transformed['bounds']))
    floor_b_support=support_evidence(base,world)
    # The already decoded Floor_A corner: validate its current native LOD using
    # ordered positions and the same independent Camera packet, not AABB overlap.
    ae_path=base/'construction-support-090d/evidence.json';ae=json.loads(ae_path.read_text());ai=ae['instances'][0]
    for source in ae['source_fingerprints']:
        if hashlib.sha256(ps.read(source['path'])).hexdigest()!=source['sha256']:raise ValueError('Native Floor_A source changed')
    af=base/'construction-support-090d'/ai['visual_id']
    for name,sha in ae['assets'][ai['visual_id']]['fingerprints'].items():
        if hashlib.sha256((af/name).read_bytes()).hexdigest()!=sha:raise ValueError('Floor_A local export changed')
    am=json.loads((af/'mesh-decoded.json').read_text())
    a=Affine(tuple(map(tuple,ai['world_linear'])),tuple(ai['world_translation']));a_matches=[]
    origin_error=np.array([.5*f32_ulp(float(v)) for v in cam])
    for mesh in am:
        local=np.asarray(mesh['attributes']['POSITION'])[mesh['indices']];expected=np.asarray([a.point(v) for v in local])
        for di,d in enumerate(native['draws']):
            if d['n']!=len(local) or d['count']!=d['n']*d['inst'] or d['skip']!=0:continue
            for inst in range(int(d['inst'])):
                start=int(d['first'])+inst*len(local);observed=native['world'][start:start+len(local)].astype(np.float64)
                if not np.isfinite(observed).all():continue
                envelope=np.asarray([a.fp32_application_bound(v) for v in local])+ae['max_raw_vs_normalized_position_delta']+origin_error+projection_bound(observed,native['ubo'])
                envelope+=(2.**-24/(1-2.**-24))*(np.abs(expected)+np.abs(cam))
                residual=np.abs(expected-cam-observed)
                if np.all(residual<=envelope):a_matches.append((mesh,di,inst,float(residual.max()),float(envelope.max())))
    if len(a_matches)!=1:raise ValueError('Existing Floor_A corner native identity absent/ambiguous')
    mesh,di,inst,error,envelope=a_matches[0]
    prepared=dict(mesh,attributes={k:v for k,v in mesh['attributes'].items() if k not in ('COLOR_0','JOINTS_0','WEIGHTS_0')})
    aw=transform_mesh(prepared,a);aw.update(tile_uuid=ai['tile']['UUID'],visual_id=ai['visual_id'],native_draw=di,native_instance=inst)
    if 'COLOR_0' in mesh['attributes']:aw['attributes']['COLOR_0']=mesh['attributes']['COLOR_0']
    world.append(aw);combined=support_evidence(base,world)
    out=base/'construction-native-ground';out.mkdir(exist_ok=True)
    geometry=out/'world-native-lod.json';geometry.write_text(json.dumps(world,separators=(',',':'))+'\n')
    summary=dict(scope='Three nearby native-qualified Floor_B POSITION pieces plus one independently matched existing Floor_A corner; NOT room closure',
        selection='Native-oracle instances only; actual-mesh bounds overlap board XZ and extend to/below its minimum Y. Bounds select; triangle intersections measure.',
        native_snapshot_sha256=oracle['binary']['sha256'],native_oracle_sha256=hashlib.sha256(oracle_path.read_bytes()).hexdigest(),
        selected_floor_b=selected,floor_a_native=dict(draw=di,instance=inst,lod=mesh['lod'],max_residual=error,max_envelope=envelope),
        floor_b_only_support=floor_b_support,combined_support=combined,
        remaining_low_vertex_samples=[i for i,sample in enumerate(combined['low_vertex_samples']) if not sample['interior_hits']],
        triangles=sum(m['triangles'] for m in world),vertices=sum(m['vertices'] for m in world),
        native_material_ids=sorted({m['material_id'] for m in world}),output=dict(path=str(geometry),sha256=hashlib.sha256(geometry.read_bytes()).hexdigest()),
        limits=['POSITION correspondence at captured native LODs only; no UV/normal/material equivalence',
                'Strict vertical triangle hits at 16 low board vertices, not continuous footprint/opacity/contact certification',
                'Surfaces above and below the board minimum are retained; no repair, snap or proxy geometry',
                'Other native geometry can underlie remaining samples; no native hole claim',
                'No roof/opening/wall enclosure or room closure proof'])
    (out/'evidence.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps({k:summary[k] for k in ('scope','triangles','vertices','floor_a_native','remaining_low_vertex_samples','native_material_ids')},indent=2))
    print('Floor_B strict low-vertex hits:',floor_b_support['strict_interior_hit_samples'],'/',floor_b_support['sample_count'])
    print('Combined strict low-vertex hits:',combined['strict_interior_hit_samples'],'/',combined['sample_count'])


def main():
    ap=argparse.ArgumentParser(__doc__);ap.add_argument('--lslib',type=Path,required=True)
    ap.add_argument('--construction',choices=('floor','wall','support','floor-b-position','floor-b-ground'),default='floor');args=ap.parse_args()
    if args.construction=='floor-b-ground':return floor_b_ground_subset(ROOT/'captures/m2-foundry')
    wall=args.construction=='wall';support=args.construction=='support';floor_b=args.construction=='floor-b-position'
    record_id=FLOOR_B_RECORD if floor_b else SUPPORT_RECORD if support else WALL_RECORD if wall else RECORD
    bank_path=FLOOR_B_BANK if floor_b else SUPPORT_BANK if support else WALL_BANK if wall else BANK
    count=1 if support or floor_b else 8 if wall else 5
    base=ROOT/'captures/m2-foundry';out=base/('construction-floor-b-position' if floor_b else 'construction-support-090d' if support else 'construction-569b-wall' if wall else 'construction-96ed');out.mkdir(parents=True,exist_ok=True)
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
    omitted=[]
    if floor_b:
        omitted=[n.to_dict() for n in native_tiles if n.get('UUID')!=FLOOR_B_TILE]
        native_tiles=[unique(native_tiles,'UUID',FLOOR_B_TILE)]
    if support:
        if len(native_tiles)!=18 or record.get('_OriginalFileVersion_')!=144115207403209018:
            raise ValueError('Selected legacy support record changed')
        omitted=[n.to_dict() for n in native_tiles if n.get('UUID')!=SUPPORT_TILE]
        native_tiles=[unique(native_tiles,'UUID',SUPPORT_TILE)]
    if len(native_tiles)!=count or record.child('Fillings').children:raise ValueError('Selected explicit construction changed')
    bank=lsf.parse(read(bank_path));tile_set=unique(bank.region('TileSetBank').children,'ID',tiles.get('TileSet'))
    tile_map={n.get('TileGUID'):n for n in tile_set.find_all('Tile')}
    if len(tile_map)!=len(list(tile_set.find_all('Tile'))):raise ValueError('Duplicate native TileGUID')
    folded={n.casefold():n for n in ps.index};assets={}
    for tile in native_tiles:
        tile_affine(tile,tiles,legacy_inactive=support,floor_b_position_only=floor_b)
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
        vid=tile_map[tile.get('tile')].get('VisualGUID');asset=assets[vid];local=tile_affine(tile,tiles,legacy_inactive=support,floor_b_position_only=floor_b)
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
    span=dict(scope='POSITION-only Floor_B; native ordered vertex oracle, not decorative-bound anchors',native_snapshot='20261031-022347',uv_flip='UNVERIFIED',native_target_lod=1) if floor_b else support_evidence(base,world_meshes) if support else wall_span_evidence(record,instances) if wall else span_evidence(record,instances)
    geometry=out/'world-lod0.json';geometry.write_text(json.dumps(world_meshes,separators=(',',':'))+'\n')
    summary=dict(scope=f'ONE explicit foundry {args.construction} construction; {count} instances/{len(assets)} native resources; NOT a room or closure certificate',
        record=record.to_dict(),source_fingerprints=sources,assets={vid:{k:v for k,v in a.items() if k!='meshes'} for vid,a in assets.items()},
        active_lod=0,instances=[{k:v for k,v in n.items() if k!='meshes'} for n in instances],
        omitted_native_tiles=omitted,
        position_policy='Verified local-X ScaleZ, no extra POSITION Flip; native snapshot target is LOD1, export below is LOD0; UV/normal/material equivalence not claimed' if floor_b else 'Original strict unit-stretch selection',
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
        legacy_field_policy='Support selection ONLY: Point1/Point2/TwoSidedTileCount absent in fixed source version; nonstretchable, unit ScaleZ, Flipfalse, Bendfalse are explicit. No active spline/extra tile semantics inferred' if support else None,
        limitations=['Native S1 POSITION correspondence qualified for Floor_B only; other modes retain authoring-only placement and visibility limits' if floor_b else 'No runtime/XFB transform or visibility agreement','Literal identity weighted-joint ancestry/bind only; nonidentity used skin/animation rejected; unused authoring bones preserved',
            'Material identity retained, construction material textures/recipe not decoded here',
            'HLOD/dynamic visibility/destruction/fade state unresolved; no opacity inferred from editor Opacity field',
            'Floor_B local-X stretch/no extra POSITION Flip qualified; UV Flip/linked/bent/regenerated geometry remain unsupported' if floor_b else 'No bent/flipped/stretched/linked spline construction or regenerated tiles','No wall/roof/opening enclosure or room closure proof',
            'Raw tile matrices preserved; normalized level quaternion convention and raw alternative retained',
            'DAE/glTF share upstream GR2 parser; native resource bounds are independent',
            'Floor_B mode qualifies POSITION only; UV/Flip texture, normals/materials and room closure remain separate' if floor_b else 'No additional native position qualification'])
    (out/'evidence.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps({k:summary[k] for k in ('scope','triangles','vertices','bounds','native_material_ids','span_evidence','max_raw_vs_normalized_position_delta')},indent=2))


if __name__=='__main__':main()
