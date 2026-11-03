// Current-frame membership/type union. Never treat unlisted b25 slots as active:
// docs/ASSET_DB.md records stale lights in those slots. No camera test at query.
[[vk::binding(23,0)]] ByteAddressBuffer clusters;
[[vk::binding(24,0)]] ByteAddressBuffer indices;
struct Constants {float4 c[34];};
[[vk::binding(26,0)]] ConstantBuffer<Constants> lighting;
[[vk::binding(31,0)]] RWByteAddressBuffer worldLights;
[numthreads(64,1,1)]
void main(uint3 tid:SV_DispatchThreadID){
 uint bytes;clusters.GetDimensions(bytes);
 float columns=lighting.c[30].z,rows=lighting.c[30].w;
 float cells=columns*rows*(float(asuint(lighting.c[31].y))+1.0);
 if(!isfinite(cells) || columns<1 || rows<1 || columns!=floor(columns) || rows!=floor(rows) || cells>float(bytes/16u)){
  if(tid.x==0u)worldLights.InterlockedOr(4096u*4u,1u);return;
 }
 if(tid.x>=uint(cells))return;
 uint4 c=clusters.Load4(tid.x*16u);
 uint np=c.y&4095u,ns=(c.y>>12)&4095u,n=np+ns,ib;indices.GetDimensions(ib);
 if(n>1024u || c.x>ib/4u || n>ib/4u-c.x){worldLights.InterlockedOr(4096u*4u,1u);return;}
 for(uint j=0;j<n;++j){uint id=indices.Load((c.x+j)*4u);
  if(id>=4096u){worldLights.InterlockedOr(4096u*4u,1u);continue;}
  worldLights.InterlockedOr(id*4u,j<np?1u:2u);
 }
}
