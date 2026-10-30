// Outward FP32 ranges. Bit steps prevent reassociation across rounded bounds.
float down(float x){if(x==0)return asfloat(0x80000001u);uint b=asuint(x);return asfloat(x>0?b-1:b+1);}
float up(float x){if(x==0)return asfloat(1u);uint b=asuint(x);return asfloat(x>0?b+1:b-1);}
float2 ip(float x){return x.xx;}
float2 ia(float2 a,float2 b){return float2(down(a.x+b.x),up(a.y+b.y));}
float2 ine(float2 a){return float2(-a.y,-a.x);}
float2 isu(float2 a,float2 b){return ia(a,ine(b));}
float2 im(float2 a,float2 b){float4 p=float4(a.x*b.x,a.x*b.y,a.y*b.x,a.y*b.y);return float2(down(min(min(p.x,p.y),min(p.z,p.w))),up(max(max(p.x,p.y),max(p.z,p.w))));}
float2 idi(float2 a,float2 b){if(b.x<=0 && b.y>=0)return float2(asfloat(0xff800000u),asfloat(0x7f800000u));return im(a,float2(down(1.0/b.y),up(1.0/b.x)));}
float2 ih(float2 a,float2 b){return float2(min(a.x,b.x),max(a.y,b.y));}
float2 idot(float3 a,float3 b){return ia(ia(im(ip(a.x),ip(b.x)),im(ip(a.y),ip(b.y))),im(ip(a.z),ip(b.z)));}
float2 idoti(float3 a,float2 b[3]){return ia(ia(im(ip(a.x),b[0]),im(ip(a.y),b[1])),im(ip(a.z),b[2]));}
void crossi(float3 a,float3 b,out float2 c[3]){
 c[0]=isu(im(ip(a.y),ip(b.z)),im(ip(a.z),ip(b.y)));
 c[1]=isu(im(ip(a.z),ip(b.x)),im(ip(a.x),ip(b.z)));
 c[2]=isu(im(ip(a.x),ip(b.y)),im(ip(a.y),ip(b.x)));
}
bool good(float2 a){return all(isfinite(a)) && a.x<=a.y;}
