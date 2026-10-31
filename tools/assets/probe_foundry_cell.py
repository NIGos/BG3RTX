"""Export ONE real open foundry bay from fixed package records + frozen S1 terrain.

No game/device calls. Persistent package geometry and frozen/current-capture-only
terrain remain distinct. No artificial walls/roof, opacity or closure assertion.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import struct
import numpy as np
from scripts.geo_view import load
from . import lsf
from .affine import Affine, UnsupportedAffine, f32_ulp, vec
from .build import DEFAULT_GAME,find_data_dir
from .lspk import PackageSet
from .extract.lights import TemplateResolver
from .probe_foundry_mesh import decode_mesh,check_collada
from .probe_foundry_construction import (SOURCE,FLOOR_B_BANK,WALL_BANK,NULL,
    hierarchy,unique,validate_export,support_evidence,SUPPORT_BANK,SUPPORT_RECORD,SUPPORT_TILE)
from .place_foundry_mesh import transform_mesh
from .check_floor_b_positions import projection_bound

SCENERY_SOURCE='Mods/GustavDev/Levels/BGH_SteelWatchFoundry_B/Scenery/_merged.lsf'
ARCH_BANK='Public/SharedDev/Content/Assets/Tilesets/Generic/[PAK]_WALL_GEN_Glasshouse_Arch_B_0n25D_4H_A/_merged.lsf'
# Native package identities, not invented cell planes/radius tests.
TILES=[
 (FLOOR_B_BANK,'2caca96a-a623-4bd6-81be-432788285a8b',u,'floor') for u in
 ('dfe85ffa-f439-599a-fc7d-b4ea1005ed5e','21cfceda-36af-b591-588c-93b7a32be058')]+[
 (FLOOR_B_BANK,'6fd38d9e-c85c-4fc8-942d-f77e610dc875',u,'floor') for u in
 ('aed4b6e9-b437-704c-c287-49fc54bbc42a','dad4d81b-9206-49bf-98c3-d5f40afee514')]+[
 (FLOOR_B_BANK,'9fc6a494-7e3f-434e-a439-470d34694558',u,'floor') for u in
 ('7fe1239e-0fef-de01-fbf8-f361a7bddc43','f56fb96c-f20b-44ea-8aab-1dc5bbde39d0','da092817-e495-44be-a6a5-44350eef780e')]+[
 (WALL_BANK,'569ba5ab-2ab9-4083-b44f-b2b42b337509',u,'west-wall') for u in
 ('d0612da3-81df-485f-9b58-5f6326e0053b','4b050c22-2c90-4887-8a5f-ecf9e102b8a4','64f0e4f3-8d7d-1f9e-92c9-f7217c66fbae')]+[
 (ARCH_BANK,'a4849383-dcb4-4c2a-9a4a-b159a93ec2e4',u,'nearby-lower-arch-opening') for u in
 ('14c27dce-5db6-25af-e997-c689dbd64ed7','4a940299-df2b-43d9-c1f4-0c5c1ec4938f','68d5782e-0f17-6e74-178f-5909b5e913cd')]
TILES += [('Public/SharedDev/Content/Assets/Tilesets/Generic/[PAK]_WALL_GEN_Glasshouse_Floor_C_4D_0n5H_A/_merged.lsf','8e988c76-d765-4d5d-88fc-bafdcb92acdb',u,'vertical-panel-not-roof') for u in ('fc57d0a0-9c7c-c9e3-ec9a-b4833ea8eaad','e2dde3f0-955c-1005-16fd-d51e10ac9e8e','a3e64743-bd90-cab3-caaf-39bda451eead','e1090c21-5004-87b2-eedf-31d9cf13f1d3','93e33b5f-9adf-b725-b49c-8252976102f8')]
TILES.append((SUPPORT_BANK,SUPPORT_RECORD,SUPPORT_TILE,'floor-corner'))
GENERIC='Public/Shared/Content/Assets/Buildings/[PAK]_Generic/_merged.lsf'
FOUNDRY='Public/SharedDev/Content/Assets/Buildings/[PAK]_Foundry/_merged.lsf'
GLASSHOUSE='Public/SharedDev/Content/Assets/Buildings/[PAK]_Glasshouse/_merged.lsf'
SCENERY=[('Public/Shared/Content/Assets/Furniture/[PAK]_Hospital/_merged.lsf','039990fb-1884-4db2-92c8-1d35014d1c02','reference-board'),(GENERIC,'44dff253-f3fd-4a77-a061-59cb82d33887','platform-under-sample5')]+[
 (FOUNDRY,u,'pillar-or-base') for u in ('2d535c31-4096-47b4-898b-4b11e7831ecd','bd98ab38-8258-42be-bdf2-83855144c59a','376abcde-bc36-4919-8c9e-93ea62bbb798','59bdf02f-2dfb-493d-8c57-180b468945e6','9b77ebd3-b49a-4df3-9a53-c9380562e389','8829caa4-8014-4ca5-ae96-ce48746f9116')]+[
 (GLASSHOUSE,'42671994-3de6-477a-a5b0-52e3129fec0b','nearby-support-beam'),
 (GLASSHOUSE,'b18a1849-aaa9-4c4b-8fe1-af5cd68f21cd','overhead-support-arch-not-roof')]


def candidate_pose(tile,tiles,normalize=True):
    # A hypothesis until independently checked against ordered native vertices.
    if tiles.get('ConstructionBend') is not False or tiles.get('ConstructionPlaceTwoTiles') is not False:
        raise UnsupportedAffine('Bent/two-tile cell construction')
    if any(tile.get(k,NULL)!=NULL for k in ('Point1','Point2')) or tile.get('TwoSidedTileCount',0)!=0:
        raise UnsupportedAffine('Linked/two-sided cell construction')
    if ('Rotate' in tile)==('QRotate' in tile):raise UnsupportedAffine('Missing/ambiguous rotation encoding')
    scale=tile.get('Scale');stretch=tile.get('ScaleZ')
    if any(not isinstance(x,(float,int)) or not math.isfinite(x) or x<=0 for x in (scale,stretch)):
        raise UnsupportedAffine('Invalid cell scale')
    if 'Rotate' in tile:
        r=vec(tile['Rotate'],9);a=Affine(tuple(tuple(r[c*3+i]*scale for c in range(3)) for i in range(3)),vec(tile['Translate'],3))
    else:a=Affine.from_node(dict(Position=tile['Translate'],RotationQuat=tile['QRotate'],Scale=scale),normalize)
    return Affine(tuple(tuple(v*(stretch if j==0 else 1) for j,v in enumerate(row)) for row in a.linear),a.translation)


def finite_triangle_slice(triangles,xz_bounds,clip=None):
    """Whole real triangles only; bounds select, never become proxy geometry."""
    finite=np.isfinite(triangles).all(axis=(1,2))
    if clip is not None:finite &= np.isfinite(clip).all(axis=(1,2))
    with np.errstate(invalid='ignore'):
        lo=np.min(triangles[:,:,[0,2]],axis=1);hi=np.max(triangles[:,:,[0,2]],axis=1)
    return np.flatnonzero(finite & np.all(hi>=xz_bounds[0],axis=1) & np.all(lo<=xz_bounds[1],axis=1))


def select_archive_terrain(mesh,evidence,xz_bounds):
    """Keep complete source triangles, indices and native primitive provenance."""
    positions=np.asarray(mesh['attributes']['POSITION'],dtype=float)
    indices=np.asarray(mesh['indices'],dtype=int).reshape(-1,3)
    kept=finite_triangle_slice(positions[indices],xz_bounds)
    if not len(kept):raise ValueError('Persistent terrain misses selected cell')
    used=np.unique(indices[kept]);remap={int(old):i for i,old in enumerate(used)}
    mapping={(r['source_cell'],r['source_fan']):r['native_primitive'] for r in evidence['native_primitive_mapping']}
    cells=[mesh['source_cell_indices'][i] for i in kept];fans=[mesh['source_fan_indices'][i] for i in kept]
    result=dict(mesh,component_id='archive_mainfloor',source_vertex_ids=used.tolist(),source_triangle_ids=kept.tolist(),
                attributes={k:[v[i] for i in used] for k,v in mesh['attributes'].items()},
                indices=[remap[int(i)] for i in indices[kept].ravel()],vertices=len(used),triangles=len(kept),
                source_cell_indices=cells,source_fan_indices=fans,
                native_triangle_ids=[mapping[(c,f)] for c,f in zip(cells,fans)])
    result['bounds']=[[float(f(positions[used,j])) for j in range(3)] for f in (np.min,np.max)]
    return result



def native_window_matches(wanted,base_error,native):
    """Exact ordered geometry inside a merged draw; no transform is fitted."""
    world=native['world'];draws=native['draws'];matches=[];seen=set()
    maximum=native.get('world_abs_bound')
    if maximum is None:maximum=float(np.max(np.abs(world[np.isfinite(world)])))
    # Monotone absolute-error enclosure over the known finite input coordinate
    # box supplies a broad search bound. Actual acceptance remains pointwise.
    search_error=base_error[0]+projection_bound(np.full((1,3),maximum),native['ubo'])[0]
    for start in range(0,len(world),65536):
        chunk=world[start:start+65536].astype(float)
        with np.errstate(invalid='ignore'):
            candidates=np.flatnonzero(np.all(np.abs(chunk-wanted[0])<=search_error,axis=1))+start
        for vertex in candidates:
            first=int(vertex)-int(vertex)%3
            if first in seen:continue
            seen.add(first)
            owner=next(((di,d) for di,d in enumerate(draws) if d['skip']==0 and d['n']>0 and d['first']<=first and first+len(wanted)<=d['first']+d['count']),None)
            if owner is None:continue
            di,d=owner;offset=first-int(d['first']);instance=offset//int(d['n'])
            if offset%int(d['n'])+len(wanted)>d['n']:continue
            observed=world[first:first+len(wanted)].astype(float)
            if not np.isfinite(observed).all() or not np.isfinite(native['clip'][first:first+len(wanted)]).all():continue
            # A cyclic per-triangle corner rotation preserves winding. Never
            # reverse or refit vertices to manufacture an identity match.
            for rotation in range(3):
                ordered=np.roll(observed.reshape(-1,3,3),-rotation,axis=1).reshape(-1,3)
                envelope=base_error+projection_bound(ordered,native['ubo']);error=np.abs(wanted-ordered)
                if np.all(error<=envelope):
                    matches.append(dict(draw=di,instance=instance,first_vertex=first,within_instance_vertex=offset%int(d['n']),corners=len(wanted),native_corner_rotation=rotation,kind='ordered_merged_window',max_residual=float(error.max()),max_envelope=float(envelope.max()),vs=f'{d["vs"]:016x}',fs=f'{d["fs"]:016x}'))
                    break
    return matches


def main():
    ap=argparse.ArgumentParser(__doc__);ap.add_argument('--capture',type=Path,required=True)
    ap.add_argument('--lslib',type=Path,required=True);ap.add_argument('--assets',type=Path,default=Path('captures/m2-foundry'))
    ap.add_argument('--persistent-foundry-terrain',action='store_true',help='Use separately qualified authored render patches for draw803 only')
    args=ap.parse_args();base=args.assets.resolve();out=base/('reference-cell-archive-terrain' if args.persistent_foundry_terrain else 'reference-cell');out.mkdir(exist_ok=True)
    binary=args.capture/'geo/scene.bin'
    # Check declared counts/bytes before the existing bulk loader allocates arrays.
    oracle=json.loads((base/'floor-b-native-oracle/native-position-oracle.json').read_text())
    if binary.stat().st_size!=oracle['binary']['bytes']:raise ValueError('Unexpected native snapshot size')
    with binary.open('rb') as f:h=struct.unpack('<8I',f.read(32))
    if h[0]!=0x47334742 or h[1]!=1 or h[6]!=1 or h[2]%3 or h[2]>50331648 or h[3]>16384 or binary.stat().st_size!=320+48*h[3]+28*h[2]:
        raise ValueError('Unsupported native snapshot header')
    if hashlib.sha256(binary.read_bytes()).hexdigest()!=oracle['binary']['sha256']:
        raise ValueError('Native cell snapshot does not match qualified S1 binary')
    native=load(binary);cam=native['ubo'][52:55].astype(float)
    native['world_abs_bound']=float(np.max(np.abs(native['world'][np.isfinite(native['world'])])))
    packed=args.lslib.resolve();tools=packed/'Tools'
    if hashlib.sha256((tools/'LSLib.dll').read_bytes()).hexdigest()!='5dfe246b93cbc95531e011dbf18478c665a9de4f922f8b4a2b73da5a865c76d0':raise ValueError('Unpinned LSLib')
    env=dict(os.environ,PATH=str(packed)+os.pathsep+str(tools)+os.pathsep+os.environ['PATH'])
    ps=PackageSet(find_data_dir(DEFAULT_GAME));sources={};banks={};assets={};folded={n.casefold():n for n in ps.index}
    def read(path):
        raw=ps.read(path)
        if len(raw)>64*1024**2:raise ValueError('Cell source exceeds 64 MiB bound')
        sources[path]=dict(sha256=hashlib.sha256(raw).hexdigest(),bytes=len(raw),package=ps.owner(path));return raw
    def bank(path):
        if path not in banks:banks[path]=lsf.parse(read(path))
        return banks[path]
    def asset(path,vid):
        if vid in assets:return assets[vid]
        visual=unique(bank(path).region('VisualBank').children,'ID',vid);folder=out/'assets'/vid;folder.mkdir(parents=True,exist_ok=True)
        name=folded[visual.get('SourceFile').casefold()];raw=read(name);(folder/'mesh.gr2').write_bytes(raw)
        for fmt in ('gltf','dae'):
            result=subprocess.run([str(tools/'Divine.exe'),'-g','bg3','-a','convert-model','-s',str(folder/'mesh.gr2'),'-d',str(folder/f'mesh.{fmt}'),'-i','gr2','-o',fmt],env=env,capture_output=True,text=True)
            (folder/f'export-{fmt}.log').write_text(result.stdout+result.stderr);result.check_returncode()
        meshes=decode_mesh(folder/'mesh.gltf',visual,True,True);doc=json.loads((folder/'mesh.gltf').read_text())
        validate_export(doc,visual,[(folder/b['uri']).read_bytes() for b in doc['buffers']],True)
        assets[vid]=dict(visual=visual.to_dict(),source=name,meshes=meshes,dae=check_collada(folder/'mesh.dae',meshes));return assets[vid]
    level,raw_level=hierarchy(json.loads((base/'world-placement.json').read_text()),ps)
    recs={r.get('MapKey'):r for r in lsf.parse(read(SOURCE)).find_all('GameObjects')};placements=[];unresolved=[]
    for path,rid,uid,role in TILES:
        record=recs[rid];tiles=record.child('tiles');tile=unique(tiles.children_named('tile'),'UUID',uid)
        ts=unique(bank(path).region('TileSetBank').children,'ID',tiles.get('TileSet'));ref=unique(ts.find_all('Tile'),'TileGUID',tile.get('tile'))
        placements.append(dict(id=uid,tile_uuid=uid,record=rid,role=role,bank=path,visual_id=ref.get('VisualGUID'),affine=level.compose(candidate_pose(tile.attrs,tiles)),raw=raw_level.compose(candidate_pose(tile.attrs,tiles,False))))
    scenery={n.get('MapKey'):n for n in lsf.parse(read(SCENERY_SOURCE)).find_all('GameObjects')};resolver=TemplateResolver(ps,types=('scenery',))
    for path,uid,role in SCENERY:
        node=scenery[uid];chain=[node]+list(resolver.chain(node.get('TemplateName','')))
        vid=next(n.get('VisualTemplate') for n in chain if n.get('VisualTemplate'))
        placements.append(dict(id=uid,role=role,bank=path,visual_id=vid,affine=level.compose(Affine.from_node(node.child('Transform'))),raw=raw_level.compose(Affine.from_node(node.child('Transform'),False))))
    origin_error=np.array([.5*f32_ulp(float(x)) for x in cam]);qualified=[];authoring=[];records=[]
    for place in placements:
        try:a=asset(place['bank'],place['visual_id'])
        except (ValueError,UnsupportedAffine) as exc:unresolved.append(dict(id=place['id'],role=place['role'],reason=str(exc)));continue
        per_lod={}
        for mesh in a['meshes']:
            p=np.asarray(mesh['attributes']['POSITION'])[mesh['indices']];expected=p@np.array(place['affine'].linear).T+place['affine'].translation
            raw=p@np.array(place['raw'].linear).T+place['raw'].translation
            base_error=np.asarray([place['affine'].fp32_application_bound(v) for v in p])+np.abs(expected-raw)+origin_error
            base_error+=(2.**-24/(1-2.**-24))*(np.abs(expected)+np.abs(cam));matches=[]
            for di,d in enumerate(native['draws']):
                if d['n']!=len(p) or d['count']!=d['n']*d['inst'] or d['skip']!=0:continue
                for inst in range(int(d['inst'])):
                    start=int(d['first'])+inst*len(p);observed=native['world'][start:start+len(p)].astype(float)
                    if not np.isfinite(observed).all() or not np.isfinite(native['clip'][start:start+len(p)]).all():continue
                    if np.any(np.abs(expected[0]-cam-observed[0])>base_error[0]+projection_bound(observed[:1],native['ubo'])[0]):continue
                    envelope=base_error+projection_bound(observed,native['ubo']);error=np.abs(expected-cam-observed)
                    if np.all(error<=envelope):matches.append(dict(draw=di,instance=inst,first_vertex=start,corners=len(p),max_residual=float(error.max()),max_envelope=float(envelope.max()),vs=f'{d["vs"]:016x}',fs=f'{d["fs"]:016x}'))
            if not matches:matches=native_window_matches(expected-cam,base_error,native)
            per_lod.setdefault(mesh['lod'],[]).append((mesh,matches))
        complete=[lod for lod,parts in per_lod.items() if all(len(matches)==1 for mesh,matches in parts) and len({(matches[0]['draw'],matches[0]['first_vertex'],matches[0]['corners']) for mesh,matches in parts})==len(parts)]
        native_lod=complete[0] if len(complete)==1 else None
        chosen=native_lod if native_lod is not None else min(per_lod)
        for mesh,matches in per_lod[chosen]:
            prepared=dict(mesh,attributes={k:v for k,v in mesh['attributes'].items() if k not in ('COLOR_0','JOINTS_0','WEIGHTS_0')});w=transform_mesh(prepared,place['affine'])
            if 'COLOR_0' in mesh['attributes']:w['attributes']['COLOR_0']=mesh['attributes']['COLOR_0']
            w.update(component_id=place['id'],tile_uuid=place.get('tile_uuid'),visual_id=place['visual_id'],role=place['role'],native_match=matches[0] if native_lod is not None else None,position_source='package_affine',visibility='captured_current' if native_lod is not None else 'UNKNOWN_AUTHORING_ONLY')
            (qualified if native_lod is not None else authoring).append(w)
        records.append(dict(id=place['id'],role=place['role'],visual_id=place['visual_id'],native_lod=native_lod,complete_lods=complete,parts=[dict(object_id=m['object_id'],lod=lod,matches=matches) for lod,parts in per_lod.items() for m,matches in parts]))
    if not qualified:raise ValueError('No real package cell geometry qualified')
    # Selection window is derived from real component bounds, not emitted as a box.
    all_components=qualified+authoring;bounds=[[f(m['bounds'][side][j] for m in all_components) for j in range(3)] for side,f in enumerate((min,max))]
    terrain_draws=[(i,d) for i,d in enumerate(native['draws']) if f'{d["vs"]:016x}'=='64e2568916ede3d6']
    archive=[];terrain_evidence=None
    if args.persistent_foundry_terrain:
        folder=base/'construction-archive-terrain';terrain_evidence=json.loads((folder/'evidence.json').read_text())
        if terrain_evidence['native_snapshot_sha256']!=oracle['binary']['sha256']:raise ValueError('Persistent terrain has another native oracle')
        geometry=folder/'world-render-terrain.json';raw=geometry.read_bytes()
        if hashlib.sha256(raw).hexdigest()!=terrain_evidence['geometry_output']['sha256']:raise ValueError('Persistent terrain geometry changed')
        for name,expected in terrain_evidence['implementation_sha256'].items():
            if hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()!=expected:raise ValueError('Persistent terrain implementation changed')
        for name,expected in terrain_evidence['source_fingerprints'].items():
            if hashlib.sha256(read(name)).hexdigest()!=expected['sha256']:raise ValueError('Persistent terrain archive changed')
        archive=[select_archive_terrain(json.loads(raw),terrain_evidence,np.asarray(bounds)[:,[0,2]])]
    frozen=[]
    for di,d in terrain_draws:
        if archive and di==803:continue
        points=native['world'][int(d['first']):int(d['first']+d['count'])].astype(float)+cam;tri=points.reshape(-1,3,3)
        source_clip=native['clip'][int(d['first']):int(d['first']+d['count'])].reshape(-1,3,4)
        world_finite=finite_triangle_slice(tri,np.asarray(bounds)[:,[0,2]])
        kept=finite_triangle_slice(tri,np.asarray(bounds)[:,[0,2]],source_clip)
        selected=tri[kept].reshape(-1,3)
        if not len(selected):continue
        # Unproject intentionally ignores z. Finite world coordinates alone
        # therefore do not certify an active primitive when native clip.z is NaN.
        relative=selected-cam;m=native['ubo'][32:48].astype(float).reshape(4,4)
        z=relative@m[:3,2]+m[3,2];z_source=source_clip[kept].reshape(-1,4)[:,2]
        gamma7=7*2.**-24/(1-7*2.**-24)
        z_bound=gamma7*(np.abs(relative)@np.abs(m[:3,2])+abs(m[3,2]))+projection_bound(relative,native['ubo'])@np.abs(m[:3,2])
        if np.any(np.abs(z-z_source)>z_bound):raise ValueError('Unsupported terrain clip-depth modification/camera frame')
        if not len(selected):continue
        frozen.append(dict(name=f'native_terrain_draw{di}',component_id=f'frozen_native_terrain_draw{di}',position_source='FROZEN_CURRENT_CAPTURE_ONLY',material_id=None,material_status='UNKNOWN_NOT_DEFAULT_OPAQUE',native_vs=f'{d["vs"]:016x}',native_fs=f'{d["fs"]:016x}',native_draw=di,excluded_nonfinite_clip_triangles=len(world_finite)-len(kept),z_residual_max=float(np.max(np.abs(z-z_source))) if len(z) else 0.,native_triangle_ids=kept.tolist(),native_first_vertex=int(d['first']),native_vertices_per_instance=int(d['n']),attributes=dict(POSITION=selected.tolist()),indices=list(range(len(selected))),vertices=len(selected),triangles=len(selected)//3,bounds=[[float(f(selected[:,j])) for j in range(3)] for f in (np.min,np.max)]))
    for name,meshes in [('world-native-qualified.json',qualified),('world-authoring-visibility-unknown.json',authoring),('world-frozen-terrain.json',frozen),('world-archive-terrain.json',archive)]:
        (out/name).write_text(json.dumps(meshes,separators=(',',':'))+'\n')
    # Surface intersections, not collision/contact or an opacity certificate.
    ground=support_evidence(base,[m for m in qualified if m['role']!='reference-board']+frozen+archive)
    terrain_path='Mods/GustavDev/Levels/BGH_SteelWatchFoundry_B/Terrains/_merged.lsf'
    terrain_node=unique(lsf.parse(read(terrain_path)).find_all('GameObjects'),'MapKey','6675c295-3445-49ef-92ec-1b27be2c48c5')
    terrain_files=[n for n in ps.index if 'BGH_SteelWatchFoundry_B/Terrains/6675c295-3445-49ef-92ec-1b27be2c48c5_' in n]
    for path in terrain_files:read(path)
    result=dict(implementation_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),scope='ONE real open foundry bay; no invented enclosure',native_snapshot=oracle['binary'],camera_origin=cam.tolist(),selection_bounds=bounds,placements=records,unresolved=unresolved,
        assets={k:{n:v for n,v in a.items() if n!='meshes'} for k,a in assets.items()},source_fingerprints=sources,
        terrain_package_inputs=dict(record=terrain_node.to_dict(),files=terrain_files,status='PERSISTENT_MAINFLOOR_POSITION_QUALIFIED_MATERIAL_UNKNOWN' if archive else 'ARCHIVES_PRESENT_RENDER_HEIGHT_UV_MATERIAL_DECODE_NOT_IMPLEMENTED'),
        archive_terrain_triangles=sum(m['triangles'] for m in archive),archive_terrain_evidence=terrain_evidence,qualified_components=len(qualified),qualified_triangles=sum(m['triangles'] for m in qualified),authoring_unknown_components=len(authoring),frozen_terrain_triangles=sum(m['triangles'] for m in frozen),ground=ground,
        limits=['POSITION only; native shading normals/material/UV equivalence not certified','Mainfloor uses authored float32 render heights when explicitly selected; GPU R16 quantizer not certified; remaining parent terrain stays frozen' if archive else 'Terrain is frozen/current-capture only, not persistent package heightfield reconstruction','No overhead roof was invented; support arch is not a ceiling','Parent-level roof/current cutaway state remains unresolved',
               'Absent native correspondence is unknown visibility/pose/submission, not proof of culling','Unknown material is not silently treated as opaque','No closed-room or contact/opacity claim'])
    (out/'evidence.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('scope','selection_bounds','qualified_components','qualified_triangles','authoring_unknown_components','frozen_terrain_triangles','archive_terrain_triangles','unresolved')},indent=2))
    print('Strict low-vertex intersections:',ground['strict_interior_hit_samples'],'/',ground['sample_count'])


if __name__=='__main__':main()
