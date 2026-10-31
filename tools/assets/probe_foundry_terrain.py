"""Persistent Foundry render patches -> geometry, checked against native S1 draw803.

No physics substitution, game/device calls, guessed exact GPU quantizer or
terrain material decoder. Native grid/topology/holes are checked independently.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import numpy as np
from scripts.geo_view import load
from . import lsf
from .lspk import PackageSet
from .build import DEFAULT_GAME,find_data_dir
from .affine import Affine,f32_ulp
from .probe_foundry_construction import hierarchy,unique
from .check_floor_b_positions import projection_bound
from .terrain_patch import read_foundry_render_patch,assemble_foundry_heightfield,heightfield_mesh,FAN_PATTERNS

TERRAIN='6675c295-3445-49ef-92ec-1b27be2c48c5'
PREFIX='Mods/GustavDev/Levels/BGH_SteelWatchFoundry_B/Terrains/'


def main():
    ap=argparse.ArgumentParser(__doc__);ap.add_argument('--capture',type=Path,required=True)
    ap.add_argument('--assets',type=Path,default=Path('captures/m2-foundry'));args=ap.parse_args()
    base=args.assets.resolve();out=base/'construction-archive-terrain';out.mkdir(exist_ok=True)
    ps=PackageSet(find_data_dir(DEFAULT_GAME));sources={}
    def read(path):
        raw=ps.read(path)
        if len(raw)>64*1024**2:raise ValueError('Terrain source exceeds bound')
        sources[path]=dict(bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest(),package=ps.owner(path));return raw
    node=unique(lsf.parse(read(PREFIX+'_merged.lsf')).find_all('GameObjects'),'MapKey',TERRAIN)
    patches=[read_foundry_render_patch(read(PREFIX+TERRAIN+'_'+suffix+'.patch')) for suffix in ('0_0','0_1')]
    field=assemble_foundry_heightfield(patches);mesh=heightfield_mesh(field)
    level,raw_level=hierarchy(json.loads((base/'world-placement.json').read_text()),ps)
    a=level.compose(Affine.from_node(node.child('Transform')));raw_a=raw_level.compose(Affine.from_node(node.child('Transform'),False))
    visual=node.child('Visual');bounds=visual.child('LocalBound')
    ylo,yhi=bounds.get('BoundsMin')[1],bounds.get('BoundsMax')[1]
    if min(field['heights'])<ylo or max(field['heights'])>yhi:raise ValueError('Render heights escape native terrain bound')
    oracle=json.loads((base/'floor-b-native-oracle/native-position-oracle.json').read_text());binary=args.capture/'geo/scene.bin'
    if binary.stat().st_size!=oracle['binary']['bytes'] or hashlib.sha256(binary.read_bytes()).hexdigest()!=oracle['binary']['sha256']:raise ValueError('Unqualified S1 snapshot')
    native=load(binary);cam=native['ubo'][52:55].astype(float);draw=native['draws'][803]
    if (int(draw['vs']),int(draw['fs']))!=(0x64e2568916ede3d6,0x6650c75f3d3e4968):raise ValueError('Wrong native terrain producer')
    # Exact retained image metadata identifies normalized16 height storage.
    frame=json.loads((args.capture/'frame.json').read_text());events=[]
    def walk(x):
        if isinstance(x,dict):
            if x.get('vs')=='64e2568916ede3d6' and x.get('fs')=='6650c75f3d3e4968':events.append(x)
            for v in x.values():walk(v)
        elif isinstance(x,list):
            for v in x:walk(v)
    walk(frame['submits'])
    if len(events)!=1:raise ValueError('Ambiguous native terrain image binding')
    image_id=events[0]['res']['s4b2'].split(':',1)[1];image=frame['images'][image_id]
    if (image['fmt'],image['w'],image['h'])!=(70,52,88):raise ValueError('Unqualified native height encoding')
    start=int(draw['first']);count=int(draw['count']);clip=native['clip'][start:start+count].reshape(-1,3,4)
    soup=native['world'][start:start+count].reshape(-1,3,3).astype(float)
    active=np.isfinite(clip).all((1,2)) & np.isfinite(soup).all((1,2));primitive_ids=np.flatnonzero(active)
    observed=soup[active];flat=observed.reshape(-1,3);world=flat+cam;inv=np.linalg.inv(np.asarray(a.linear))
    local=(world-a.translation)@inv.T
    # Derived pose/encoding enclosure, independent of measured residuals.
    box=list(itertools.product((0.,49.),(ylo,yhi),(0.,84.)))
    pose_error=np.max(np.asarray([a.fp32_application_bound(p) for p in box]),axis=0)
    pose_error+=np.max(np.abs(np.asarray([a.point(p) for p in box])-np.asarray([raw_a.point(p) for p in box])),axis=0)
    pose_error+=np.asarray([.5*f32_ulp(float(v)) for v in cam])
    world_error=pose_error+projection_bound(flat,native['ubo'])
    g64=64*2.**-53/(1-64*2.**-53)
    local_error=world_error@np.abs(inv.T)+g64*np.linalg.cond(np.asarray(a.linear),np.inf)*np.max(np.abs(local),axis=1)[:,None]
    key=np.rint(local[:,[0,2]]*2).astype(int)
    if np.any(local_error[:,[0,2]]>=.25) or np.any(np.abs(local[:,[0,2]]-key*.5)>local_error[:,[0,2]]):raise ValueError('Ambiguous native half-grid coordinate')
    model={tuple(k):i for i,k in enumerate(mesh['attributes']['TERRAIN_GRID_HALF'])}
    native_keys=[];height_errors=[];world_errors=[];envelopes=[];seen=set();mapping=[]
    # One enclosing registered-height-domain code is a declared fixture
    # comparison allowance. Actual scale/bias UBO and quantizer are not retained;
    # this is not a universal sampler/precision certificate.
    code_envelope=(yhi-ylo)/65535.;y_axis=np.abs(np.asarray(a.linear)[:,1])
    all_positions=np.asarray(mesh['attributes']['POSITION'])
    for i,(tri,tri_keys) in enumerate(zip(observed,key.reshape(-1,3,2))):
        centers=[p for p in tri_keys if all(p%2==1)]
        if len(centers)!=1:raise ValueError('Native terrain is not the qualified center fan')
        cell_xy=(centers[0]-1)//2;cell=int(cell_xy[1])*50+int(cell_xy[0])
        pattern=tuple(map(tuple,(tri_keys-cell_xy*2).tolist()))
        if pattern not in FAN_PATTERNS:raise ValueError('Native fan winding/topology differs')
        fan=FAN_PATTERNS.index(pattern);identity=(cell,fan)
        if identity in seen:raise ValueError('Duplicate native terrain primitive')
        seen.add(identity);native_keys.append(identity)
        ids=[model[tuple(p)] for p in tri_keys];points=all_positions[ids]
        expected=points@np.asarray(a.linear).T+a.translation
        error=np.abs(expected-cam-tri)
        envelope=world_error[i*3:i*3+3]+code_envelope*y_axis
        if np.any(error>envelope):raise ValueError('Native terrain position differs beyond declared encoding envelope')
        height_errors.extend(np.abs(points[:,1]-local[i*3:i*3+3,1]).tolist())
        world_errors.append(float(error.max()));envelopes.append(float(envelope.max()))
        mapping.append(dict(native_primitive=int(primitive_ids[i]),source_cell=cell,source_fan=fan))
    expected_keys=set(zip(mesh['source_cell_indices'],mesh['source_fan_indices']))
    if seen!=expected_keys:raise ValueError('Native/archive hole coverage or triangle inventory differs')
    expected_centers={(2*(c%50)+1,2*(c//50)+1) for c in mesh['source_cell_indices']}
    actual_centers={tuple(p) for p in key if all(p%2==1)}
    if expected_centers!=actual_centers:raise ValueError('Native/archive center mask differs')
    positions=np.asarray(mesh['attributes']['POSITION']);world_positions=positions@np.asarray(a.linear).T+a.translation
    mesh['attributes']['POSITION']=world_positions.tolist();mesh['bounds']=[[float(f(world_positions[:,j])) for j in range(3)] for f in (np.min,np.max)]
    mesh.update(terrain_id=TERRAIN,source_files=list(sources),coordinate_space='CTY_Main_A authored world',native_draw=803)
    geometry=out/'world-render-terrain.json';geometry.write_text(json.dumps(mesh,separators=(',',':'))+'\n')
    by_patch=[]
    for p in patches:
        chosen=[i for i,k in enumerate(native_keys) if p.patch_z*64<=k[0]//50<p.patch_z*64+p.sample_height-1]
        by_patch.append(dict(patch_z=p.patch_z,holes=len(p.hole_indices),active_triangles=len(chosen),max_world_residual=max(world_errors[i] for i in chosen),max_envelope=max(envelopes[i] for i in chosen),material_tail_bytes=len(p.material_tail)))
    report=dict(scope='Qualified Foundry v8 persistent render geometry, not physics/material decoding',terrain_id=TERRAIN,source_fingerprints=sources,native_snapshot_sha256=oracle['binary']['sha256'],native_draw=803,height_image=dict(id=image_id,**image),
        vertices=mesh['vertices'],triangles=mesh['triangles'],holes=len(field['holes']),non_hole_cells=len(actual_centers),native_primitive_mapping=mapping,patches=by_patch,
        max_height_residual=float(max(height_errors)),max_world_residual=max(world_errors),max_declared_world_envelope=max(envelopes),declared_height_code_envelope=code_envelope,
        geometry_output=dict(path=str(geometry),sha256=hashlib.sha256(geometry.read_bytes()).hexdigest()),
        implementation_sha256={n:hashlib.sha256(Path(__file__).with_name(n).read_bytes()).hexdigest() for n in ('terrain_patch.py','probe_foundry_terrain.py')},
        limits=['Authored float32 heights; exact GPU R16 quantizer/filter precision not certified','Geometry grid/fan/hole inventory verified for current fixed S1 native snapshot','Opaque material/layer tails and terrain brush UV/shading remain unknown','Archive geometry is not a guarantee of runtime destruction/deformation state','Physics .bin never used as render geometry'])
    (out/'evidence.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('scope','vertices','triangles','holes','non_hole_cells','patches','max_height_residual','max_world_residual','max_declared_world_envelope','declared_height_code_envelope')},indent=2))


if __name__=='__main__':main()
