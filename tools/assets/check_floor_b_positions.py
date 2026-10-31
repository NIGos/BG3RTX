"""One S1 native Floor_B POSITION oracle. No game/device calls or model fitting.

Requires the existing M2 board and floor-b-position exports. Correspondence is
ordered triangle topology plus the independent native Camera.c[13] translation.
This does not certify UV Flip, shading normals, materials or room closure.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import numpy as np
from scripts.geo_view import load
from . import lsf
from .affine import Affine, f32_ulp
from .build import DEFAULT_GAME, find_data_dir
from .lspk import PackageSet
from .probe_foundry_construction import (SOURCE,FLOOR_B_GUID,FLOOR_B_TILE,
    FLOOR_B_BANK,hierarchy,tile_affine)


def projection_bound(points, camera_ubo):
    # Native VS6044: precise four-term instance transform, then precise VP dot.
    # Bound separate FP32 products/additions; FMA can only reduce this count.
    m=np.asarray(camera_ubo[32:48],dtype=np.float64).reshape(4,4)
    a=m[:3,[0,1,3]].T;t=m[3,[0,1,3]];inv=np.linalg.inv(a)
    cond=np.linalg.norm(a,np.inf)*np.linalg.norm(inv,np.inf)
    u=2.**-24;g7=7*u/(1-7*u);g64=64*2.**-53/(1-64*2.**-53)
    if g64*cond>=1:raise ValueError('Unresolved native inverse conditioning')
    clip_error=g7*(np.abs(points)@np.abs(a.T)+np.abs(t))
    inv_error=(g64*cond/(1-g64*cond))*np.max(np.abs(points),axis=1)
    rounding=np.array([[.5*f32_ulp(float(v)) for v in row] for row in points])
    return clip_error@np.abs(inv.T)+rounding+inv_error[:,None]


def main():
    ap=argparse.ArgumentParser(__doc__)
    ap.add_argument('--capture',type=Path,required=True)
    ap.add_argument('--assets',type=Path,default=Path('captures/m2-foundry'))
    args=ap.parse_args();base=args.assets.resolve();binary=args.capture/'geo/scene.bin'
    with binary.open('rb') as f:h=struct.unpack('<8I',f.read(32))
    if h[0]!=0x47334742 or h[1]!=1 or h[6]!=1 or not h[2] or h[2]%3 or h[2]>50331648 or h[3]>16384:
        raise ValueError('Unsupported/budget-exceeding native geometry header')
    if binary.stat().st_size!=320+48*h[3]+28*h[2]:raise ValueError('Native geometry byte length mismatch')
    native=load(binary);cam=native['ubo'][52:55].astype(np.float64)
    if not np.isfinite(cam).all():raise ValueError('Invalid captured Camera.c[13]')
    origin_error=np.array([.5*f32_ulp(float(v)) for v in cam])
    ps=PackageSet(find_data_dir(DEFAULT_GAME));parent=json.loads((base/'world-placement.json').read_text())
    level,raw_level=hierarchy(parent,ps)
    evidence=json.loads((base/'construction-floor-b-position/evidence.json').read_text())
    for source in evidence['source_fingerprints']:
        if hashlib.sha256(ps.read(source['path'])).hexdigest()!=source['sha256']:raise ValueError('Native package source changed')
    vid=evidence['instances'][0]['visual_id'];folder=base/'construction-floor-b-position'/vid
    for name,sha in evidence['assets'][vid]['fingerprints'].items():
        if hashlib.sha256((folder/name).read_bytes()).hexdigest()!=sha:raise ValueError('Export fingerprint mismatch')
    meshes=json.loads((folder/'mesh-decoded.json').read_text())
    board=json.loads((base/'world-lod0.json').read_text());ix=board['indices'];board_points=np.asarray(board['attributes']['POSITION'])[ix]
    board_draws=[(i,d) for i,d in enumerate(native['draws']) if d['n']==len(ix) and d['inst']==1 and d['count']==len(ix) and d['skip']==0]
    if len(board_draws)!=1:raise ValueError('Independent ordinary board draw is absent/ambiguous')
    bi,bd=board_draws[0];observed=native['world'][int(bd['first']):int(bd['first']+bd['count'])].astype(np.float64)
    wp=json.loads((base/'world-placement.json').read_text())['world_lod_alternatives'][0]
    board_bound=wp['max_declared_fp32_application_bound']+wp['max_raw_vs_normalized_position_delta']
    be=board_bound+origin_error+projection_bound(observed,native['ubo'])
    board_residual=np.abs(board_points-cam-observed)
    if not np.all(board_residual<=be):raise ValueError('Independent board/camera correspondence failed')
    candidates=[]
    for mesh in meshes:
        local=np.asarray(mesh['attributes']['POSITION'],dtype=np.float64)[mesh['indices']]
        for di,d in enumerate(native['draws']):
            if d['n']!=len(local) or d['count']!=d['n']*d['inst'] or d['skip']!=0:continue
            for inst in range(int(d['inst'])):
                start=int(d['first'])+inst*len(local);points=native['world'][start:start+len(local)].astype(np.float64)
                if not np.isfinite(points).all():continue
                candidates.append(dict(draw=di,instance=inst,lod=mesh['lod'],native_vs=f'{d["vs"]:016x}',native_fs=f'{d["fs"]:016x}',local=local,points=points,projection_error=projection_bound(points,native['ubo'])))
    rows=[]
    for rec in lsf.parse(ps.read(SOURCE)).find_all('GameObjects'):
        tiles=rec.child('tiles')
        if not tiles:continue
        for tile in tiles.children_named('tile'):
            if tile.get('tile')!=FLOOR_B_GUID:continue
            local=tile_affine(tile,tiles,floor_b_position_only=True);a=level.compose(local);raw=raw_level.compose(local);matches=[]
            for c in candidates:
                p=c['local'];pred=np.asarray([a.point(v) for v in p]);raw_pred=np.asarray([raw.point(v) for v in p])
                envelope=np.asarray([a.fp32_application_bound(v) for v in p])+np.abs(pred-raw_pred)+origin_error+c['projection_error']
                # Native relative instance translation may be stored after subtracting
                # camera from global authoring translation. Enclose that FP32 subtraction.
                envelope+=(2.**-24/(1-2.**-24))*(np.abs(pred)+np.abs(cam))
                residual=np.abs(pred-cam-c['points'])
                if np.all(residual<=envelope):
                    alternatives={}
                    for axis in [None,1,2]:
                        t=dict(tile.attrs);t['ScaleZ']=1.;alt=tile_affine(t,tiles,floor_b_position_only=True)
                        if axis is not None:
                            linear=[list(r) for r in alt.linear]
                            for r in range(3):linear[r][axis]*=tile.get('ScaleZ')
                            alt=Affine(tuple(map(tuple,linear)),alt.translation)
                        alt=level.compose(alt);diff=np.abs(np.asarray([alt.point(v) for v in p])-cam-c['points'])
                        alternatives['none' if axis is None else 'YZ'[axis-1]]=dict(max_residual=float(diff.max()),all_vertices_within_envelope=bool(np.all(diff<=envelope)))
                    for axis in (0,2):
                        mirror=p.copy();mirror[:,axis]=p[:,axis].min()+p[:,axis].max()-p[:,axis]
                        diff=np.abs(np.asarray([a.point(v) for v in mirror])-cam-c['points'])
                        alternatives['center_reflection_'+('X' if axis==0 else 'Z')]=dict(max_residual=float(diff.max()),all_vertices_within_envelope=bool(np.all(diff<=envelope)))
                    matches.append(dict(draw=c['draw'],instance=c['instance'],lod=c['lod'],native_vs=c['native_vs'],native_fs=c['native_fs'],triangle_corners=len(p),max_residual=float(residual.max()),max_envelope=float(envelope.max()),alternatives=alternatives))
            if len(matches)>1:raise ValueError('Native Floor_B instance association is ambiguous')
            if matches:rows.append(dict(record=rec.get('MapKey'),tile_uuid=tile.get('UUID'),flip=tile.get('Flip'),scale=tile.get('Scale'),scale_z=tile.get('ScaleZ'),match=matches[0]))
    keys=[(r['match']['draw'],r['match']['instance']) for r in rows]
    if len(set(keys))!=len(keys):raise ValueError('Authored Floor_B identity is ambiguous')
    target=[r for r in rows if r['tile_uuid']==FLOOR_B_TILE]
    if len(target)!=1:raise ValueError('Chosen Floor_B native target absent')
    result=dict(scope='Native POSITION only; no fitted Floor_B transform or tolerance',native_camera=cam.tolist(),native_camera_source='scene.bin camera UBO float indices52:55 / set0 binding2 c[13].xyz',binary=dict(path=str(binary),bytes=binary.stat().st_size,sha256=hashlib.sha256(binary.read_bytes()).hexdigest()),
        independent_board=dict(draw=bi,corners=len(ix),held_out_corners=len(ix)-1,first_corner_derived_camera=(board_points[0]-observed[0]).tolist(),max_world_residual=float(board_residual.max()),max_envelope=float(be.max())),
        unique_floor_b_instances=len(rows),held_out_floor_b_instances=len(rows)-1,matched_triangle_corners=sum(r['match']['triangle_corners'] for r in rows),flip_true_instances=sum(r['flip'] for r in rows),maximum_native_residual=max(r['match']['max_residual'] for r in rows),
        source_hashes={name:hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest() for name in ('check_floor_b_positions.py','probe_foundry_construction.py','affine.py')},target=target[0],native_matches=rows,limits=['No UV/texture Flip certification','No material/normal equivalence or scene closure','Native FP32 arithmetic envelope; not native bit identity','Missing visibility is not an absent asset claim'])
    out=base/'floor-b-native-oracle';out.mkdir(exist_ok=True);(out/'native-position-oracle.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='native_matches'},indent=2))


if __name__=='__main__':main()
