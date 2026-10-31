"""Bounded BG3 v8 render-height reader for the qualified Foundry 49x84 terrain.

PVersion render .patch only, never SEBD physics .bin. Geometry fields are read;
material/layer tails are preserved opaque. Heights remain authored float32, not
a claim of bit-identical R16_UNORM GPU filtering or current dynamic terrain state.
"""
from dataclasses import dataclass
import math
import struct


class TerrainPatchError(ValueError):pass


@dataclass(frozen=True)
class RenderPatchV8:
    sample_width:int
    sample_height:int
    total_width:int
    total_height:int
    patch_x:int
    patch_z:int
    heights:tuple
    hole_indices:tuple
    header_word_52:int
    header_opaque:bytes
    material_tail:bytes


def read_foundry_render_patch(raw):
    if len(raw)<88 or raw[:8]!=b'PVersion':raise TerrainPatchError('Not a render PVersion patch')
    version,header_bytes,w,h,cw,ch,tw,th,px,pz,holes,word52=struct.unpack_from('<12I',raw,8)
    if version!=8 or header_bytes!=72:raise TerrainPatchError('Unsupported render patch header/version')
    # Only this observed native dimension/profile is qualified. No silent
    # generalization to other versions, multi-X layouts, formats or mods.
    if (tw,th,px) != (50,85,0) or pz not in (0,1):raise TerrainPatchError('Outside qualified Foundry terrain domain')
    if w!=50 or h!=(65 if pz==0 else 21) or (cw,ch)!=(w-1,h-1) or word52!=4:
        raise TerrainPatchError('Unsupported patch dimensions/header fields')
    if holes>cw*ch:raise TerrainPatchError('Hole count exceeds cells')
    height_end=88+4*w*h;hole_end=height_end+4*holes
    if hole_end>len(raw):raise TerrainPatchError('Truncated render heights/holes')
    heights=struct.unpack_from('<'+str(w*h)+'f',raw,88)
    if not all(math.isfinite(x) for x in heights):raise TerrainPatchError('Nonfinite authored height requires unsupported coverage')
    indices=struct.unpack_from('<'+str(holes)+'I',raw,height_end)
    if len(set(indices))!=len(indices):raise TerrainPatchError('Duplicate hole cell index')
    for value in indices:
        x,z=value%tw,value//tw # GLOBAL sample-row stride, including patch 1.
        if not (0<=x<cw and pz*64<=z<pz*64+ch):raise TerrainPatchError('Hole index outside this patch cell domain')
    return RenderPatchV8(w,h,tw,th,px,pz,heights,indices,word52,raw[56:88],raw[hole_end:])


def assemble_foundry_heightfield(patches):
    if len(patches)!=2 or {p.patch_z for p in patches}!={0,1}:
        raise TerrainPatchError('Both qualified render patches required')
    heights=[None]*(50*85);holes=set()
    for p in patches:
        for row in range(p.sample_height):
            for x in range(50):
                target=(p.patch_z*64+row)*50+x;value=p.heights[row*50+x]
                if heights[target] is not None and heights[target]!=value:
                    raise TerrainPatchError('Render patch seam differs; never average or snap')
                heights[target]=value
        holes.update(p.hole_indices)
    if any(x is None for x in heights):raise TerrainPatchError('Incomplete render heightfield')
    return dict(width=50,height=85,heights=heights,holes=holes,
                material_status='UNKNOWN_OPAQUE_TAIL_NOT_DECODED')


# Native VS64e input parity and all 13,512 active S1 triangles independently
# establish these four winding-preserving local patterns. One center per cell.
FAN_PATTERNS=(((0,0),(0,2),(1,1)),((1,1),(0,2),(2,2)),
              ((1,1),(2,2),(2,0)),((1,1),(2,0),(0,0)))


def heightfield_mesh(field):
    if (field['width'],field['height'])!=(50,85):raise TerrainPatchError('Unsupported assembled dimensions')
    h=field['heights'];holes=field['holes'];positions=[];keys=[];indices=[];cells=[];fans=[]
    for z in range(85):
        for x in range(50):positions.append((float(x),h[z*50+x],float(z)));keys.append((2*x,2*z))
    for z in range(84):
        for x in range(49):
            cell=z*50+x
            if cell in holes:continue
            center=len(positions)
            # Linear native half-cell sampling; authored F32 source values are
            # kept. Native R16 quantization is an explicit comparison envelope.
            y=math.fsum(h[q] for q in (z*50+x,z*50+x+1,(z+1)*50+x,(z+1)*50+x+1))*.25
            positions.append((x+.5,y,z+.5));keys.append((2*x+1,2*z+1))
            tl=z*50+x;bl=(z+1)*50+x;br=bl+1;tr=tl+1
            for fan,triangle in enumerate(((tl,bl,center),(center,bl,br),(center,br,tr),(center,tr,tl))):
                indices.extend(triangle);cells.append(cell);fans.append(fan)
    return dict(name='TR_BGH_SteelWatchFoundry_B_Mainfloor',vertices=len(positions),triangles=len(indices)//3,
                attributes=dict(POSITION=positions,TERRAIN_GRID_HALF=keys),indices=indices,
                source_cell_indices=cells,source_fan_indices=fans,material_id=None,
                material_status=field['material_status'],position_source='PERSISTENT_RENDER_PATCH_AUTHORED_FLOAT32',
                limits=['No physics mesh substitution','Not bit-identical native R16 quantization/filtering',
                        'No terrain material/UV/layer-tail or dynamic-state qualification'])


def read_foundry_layer_inputs(patch,ordered_brush_ids):
    """Four authored cell-byte planes followed by the observed BC7 normal/hole map.

    Byte planes are NOT normalized blend weights: height/threshold native rules
    still apply. Require the actual LSF order; no inferred UUID or channel binding.
    """
    if len(ordered_brush_ids)!=4 or len(set(ordered_brush_ids))!=4:
        raise TerrainPatchError('Expected four distinct native brush identities')
    tail=patch.material_tail;offset=0;planes=[]
    cells=(patch.sample_width-1)*(patch.sample_height-1)
    for expected in ordered_brush_ids:
        if offset+44>len(tail):raise TerrainPatchError('Truncated layer header')
        length=struct.unpack_from('<I',tail,offset)[0];offset+=4
        if length!=36:raise TerrainPatchError('Unexpected brush identifier encoding')
        try:identity=tail[offset:offset+36].decode('ascii')
        except UnicodeDecodeError as exc:raise TerrainPatchError('Non-ASCII brush identity') from exc
        offset+=36;count=struct.unpack_from('<I',tail,offset)[0];offset+=4
        if identity!=expected or count!=cells:raise TerrainPatchError('Brush order/cell plane differs from native LSF profile')
        if offset+count>len(tail):raise TerrainPatchError('Truncated authored layer plane')
        planes.append(dict(brush_id=identity,cell_bytes=tail[offset:offset+count]));offset+=count
    if offset+4>len(tail):raise TerrainPatchError('Missing normal/hole block payload')
    count=struct.unpack_from('<I',tail,offset)[0];offset+=4
    width=52;height=68 if patch.patch_z==0 else 24
    if count!=(width//4)*(height//4)*16 or offset+count!=len(tail):
        raise TerrainPatchError('Unexpected BC7 payload size or trailing data')
    return dict(layers=planes,cell_width=49,cell_height=patch.sample_height-1,
                normal_hole_bc7=tail[offset:],image_width=width,image_height=height)


def bc7_dds(payload,width,height):
    """Wrap an already identified BC7 UNORM block stream; no recompression."""
    if width<=0 or height<=0 or width>4096 or height>4096 or len(payload)!=((width+3)//4)*((height+3)//4)*16:
        raise TerrainPatchError('Invalid bounded BC7 image dimensions/size')
    header=[124,0x81007,height,width,len(payload),0,0]+[0]*11
    header += [32,4,struct.unpack('<I',b'DX10')[0],0,0,0,0,0]+[0x1000,0,0,0,0]
    return b'DDS '+struct.pack('<31I',*header)+struct.pack('<5I',98,3,0,1,0)+payload
