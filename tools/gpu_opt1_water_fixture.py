"""Production water compose adapter for the existing frame_replay harness.
No separate renderer: invokes the real function, preserves its native target format.
"""
from pathlib import Path
import re
import shutil
import struct

WATER_REQUEST='gpu_opt1_water_fixture.hlsl?WATER_SHADOWS=1;WATER_LS=1;WATER_REFR=1;WATER_GSH=1'
WATER_SHADER=r'''
#include "rt/water_compose.hlsl"
[numthreads(8,8,1)]
void main(uint3 tid:SV_DispatchThreadID,uint gi:SV_GroupIndex){
 // Model the already-rendered native attachment. Both specializations receive
 // exactly the same representable B10G11R11 colour before the production entry.
 if(all(tid.xy<pc.full))target[tid.xy]=float3(1.0,1.5,2.0);
 DeviceMemoryBarrierWithGroupSync();
 water_compose(tid,gi);
}
'''
WATER_OUTPUTS=('waterTarget.raw','waterStats.bin','waterCamera.bin','waterRecords.bin')
def water_fixture(src,dst,kind):
    shutil.copytree(src,dst,dirs_exist_ok=True)
    width,height=17,13 # Partial 8x8 workgroups must participate in every barrier.
    push=bytearray((dst/'push.bin').read_bytes())
    struct.pack_into('<4I',push,0,width,height,width,height)
    (dst/'push.bin').write_bytes(push)
    record_bytes=112
    records=bytearray(64+width*height*record_bytes)
    def halves(a,b):return struct.unpack('<I',struct.pack('<2e',a,b))[0]
    for y in range(height):
        for x in range(width):
            i=y*width+x;at=64+i*record_bytes
            # Every empty group contains arbitrary high roughness bits. Only
            # low validity bits authorize a record, as in the native producer.
            struct.pack_into('<I',records,at+28,0xffff0000)
            use = kind!='empty' and (x//8+y//8)%2==0 and i%3==0
            if not use:continue
            normal=(.99,-.1,0)
            words=[halves(.5,.5),halves(.5,.125),halves(.125,.125),halves(*normal[:2]),
                   halves(normal[2],1),halves(1.5,2),struct.unpack('<I',struct.pack('<f',1))[0],1]
            if kind=='invalid':words[6]=0x7fc00000 # validity remains set: cannot skip bad metadata.
            struct.pack_into('<8I',records,at,*words)
            # Full Ultra record has no sun/local/refraction term. Reflection
            # still uses its real ray, ownership and debug output paths.
            struct.pack_into('<I',records,at+108,halves(0,1)) # native game sun visibility
    (dst/'water-records.bin').write_bytes(records)
    (dst/'water-stats.bin').write_bytes(bytes(512))
    (dst/'water-camera.bin').write_bytes(bytes(1024))
    debug=25 if kind=='mixed-debug' else 0
    (dst/'water-params.bin').write_bytes(struct.pack('<4I4f',width,width*height,debug,31,.5,1,0,0))
    (dst/'water-rad.rgba16f').write_bytes(struct.pack('<4e',1,1,1,0)*width*height)
    (dst/'water-depth.r32f').write_bytes(struct.pack('<f',5)*width*height)
    text=(dst/'manifest.txt').read_text()
    text=re.sub(r'^pass .*$', 'pass water_compose_fixture',text,flags=re.M)
    text=re.sub(r'^dispatch .*$', 'dispatch 3 2 1',text,flags=re.M)
    text=re.sub(r'^output .*\n','',text,flags=re.M)
    text += f'buffer 0 60 32 water-params.bin\nbuffer 0 61 512 water-stats.bin\nbuffer 0 64 1024 water-camera.bin\nbuffer 0 66 {len(records)} water-records.bin\n'
    text += f'image 0 62 0 97 {width} {height} 1 1 water-rad.rgba16f\nimage 0 63 0 100 {width} {height} 1 1 water-depth.r32f\n'
    text += f'output 0 67 122 {width} {height}\n'
    (dst/'manifest.txt').write_text(text,encoding='utf-8')
    return dst
