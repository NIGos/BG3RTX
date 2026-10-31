"""Recover one qualified Foundry terrain's authored layers and native input identities.

CPU-only. Cell bytes are not guessed normalized weights; no native shading proof.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import numpy as np
from PIL import Image, __version__ as pillow_version
from . import lsf
from .lspk import PackageSet
from .build import find_data_dir,DEFAULT_GAME
from .probe_foundry_construction import unique
from .probe_foundry_terrain import TERRAIN,PREFIX
from .terrain_patch import read_foundry_render_patch,read_foundry_layer_inputs,bc7_dds

BANK='Public/Shared/Content/Assets/TerrainTextures/[PAK]_TerrainTextures/_merged.lsf'


def main():
    ap=argparse.ArgumentParser(__doc__);ap.add_argument('--assets',type=Path,default=Path('captures/m2-foundry'))
    ap.add_argument('--native-cache',type=Path,required=True);args=ap.parse_args()
    out=args.assets.resolve()/'terrain-material-inputs';out.mkdir(exist_ok=True)
    ps=PackageSet(find_data_dir(DEFAULT_GAME));sources={};folded={n.casefold():n for n in ps.index}
    def read(path):
        raw=ps.read(path)
        if len(raw)>64*1024**2:raise ValueError('Terrain material input exceeds bounded size')
        sources[path]=dict(bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest(),package=ps.owner(path));return raw
    terrain=unique(lsf.parse(read(PREFIX+'_merged.lsf')).find_all('GameObjects'),'MapKey',TERRAIN)
    layers=terrain.child('TerrainLayers').children_named('TerrainLayer');ids=[n.get('TerrainBrushResource') for n in layers]
    bank=lsf.parse(read(BANK));brushes=bank.region('TerrainBrushBank').children;textures=list(bank.region('TextureBank').children)
    detail=lsf.parse(read('Public/Engine/Content/[PAK]_Engine/_merged.lsf'));textures+=detail.region('TextureBank').children
    recovered=[]
    for identity,node in zip(ids,layers):
        brush=unique(brushes,'ID',identity);maps={}
        for field in ('BaseColorMap','NormalMap','PhysicalMap','DetailNormalMap'):
            texture=unique(textures,'ID',brush.get(field));path=folded[texture.get('SourceFile').casefold()];raw=read(path)
            # Preserve native DDS format/mips and byte values. Decode without a
            # colour-space/native-normal interpretation, only to verify usability.
            image=Image.open(io.BytesIO(raw));image.load()
            target=out/(texture.get('ID')+'.dds');target.write_bytes(raw)
            maps[field]=dict(texture=texture.to_dict(),source=path,decoded_mode=image.mode,decoded_size=image.size,metadata_dimensions_equal=image.size==(texture.get('Width'),texture.get('Height')),
                             decoded_rgba_sha256=hashlib.sha256(image.convert('RGBA').tobytes()).hexdigest())
        recovered.append(dict(id=identity,brush=brush.to_dict(),overrides=node.to_dict(),textures=maps))
    patches=[]
    for z in (0,1):
        patch=read_foundry_render_patch(read(PREFIX+TERRAIN+'_0_'+str(z)+'.patch'))
        inputs=read_foundry_layer_inputs(patch,ids);w,h=inputs['image_width'],inputs['image_height']
        dds=bc7_dds(inputs['normal_hole_bc7'],w,h);(out/f'patch{z}-normal-holes.dds').write_bytes(dds)
        image=Image.open(io.BytesIO(dds));rgba=np.asarray(image.convert('RGBA'));image.save(out/f'patch{z}-normal-holes.png')
        # Native VS64e uses texelFetch at integer cell coordinates, alpha<0.5.
        holes={row*50+x for row in range(z*64,z*64+inputs['cell_height']) for x in range(49) if rgba[row-z*64,x,3]<128}
        if holes!=set(patch.hole_indices):raise ValueError('Decoded BC7 alpha differs from exact archive/native hole inventory')
        planes=[]
        for i,plane in enumerate(inputs['layers']):
            data=plane['cell_bytes'];path=out/f'patch{z}-layer{i}.bin';path.write_bytes(data)
            planes.append(dict(brush_id=plane['brush_id'],path=str(path),bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),nonzero=sum(v!=0 for v in data),maximum=max(data)))
        patches.append(dict(patch_z=z,cell_dimensions=[49,inputs['cell_height']],layers=planes,normal_holes=dict(width=w,height=h,format='BC7_UNORM',payload_sha256=hashlib.sha256(inputs['normal_hole_bc7']).hexdigest(),decoded_rgba_sha256=hashlib.sha256(rgba.tobytes()).hexdigest(),holes=len(holes),hole_inventory_exact=True)))
    native={}
    for shader in ('64e2568916ede3d6','6650c75f3d3e4968'):
        p=args.native_cache/(shader+'.glsl');raw=p.read_bytes();native[shader]=dict(path=str(p),sha256=hashlib.sha256(raw).hexdigest())
    report=dict(scope='ONE persistent Foundry mainfloor native input recovery, not complete terrain shading',terrain_id=TERRAIN,
        source_fingerprints=sources,brushes=recovered,patches=patches,native_glsl=native,pillow_version=pillow_version,
        native_bindings=dict(height='s4b2',normal_holes='s4b3',painted_layer_mask='s1b2',base_curve_lut='s1b3',brush_records='s1b4',textures='s2b32[]'),
        limits=['Authored cell byte planes are not normalized blend weights or proved native channel mapping',
                'Native brush record packing, mask upload/filtering and derivative-dependent triplanar sampling remain unimplemented',
                'DDS byte decode is not native colour-space/normal/physical arithmetic equivalence',
                'BC7 alpha holes exact against archive hole indices and prior native draw803 topology; RGB normal geometry interpretation remains separate',
                'Parent terrain/roof/current cutaway state not addressed; no opacity default'])
    (out/'evidence.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(brushes=[dict(id=b['id'],name=b['brush']['Name']) for b in recovered],patches=[dict(z=p['patch_z'],holes=p['normal_holes']['holes'],planes=len(p['layers'])) for p in patches],source_files=len(sources),decoded_texture_ids=len({t['texture']['ID'] for b in recovered for t in b['textures'].values()})),indent=2))


if __name__=='__main__':main()
