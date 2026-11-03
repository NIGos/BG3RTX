#ifndef BG3_ANIMATED_MATERIAL_H
#define BG3_ANIMATED_MATERIAL_H
// Audited UV arithmetic only; texture/view/geometry/time provenance belongs to
// the caller. Game time is 0:0 byte 0, never wall time or pc.frame.
#ifdef __cplusplus
#include <cmath>
namespace bg3::animated {
inline float am_floor(float x){return std::floor(x);}
inline float am_trunc(float x){return std::trunc(x);}
#else
float am_floor(float x){return floor(x);}
float am_trunc(float x){return trunc(x);}
#endif
struct AmUv {float x,y;};
inline AmUv am_uv(float x,float y){AmUv r;r.x=x;r.y=y;return r;}
// Base_VT_Scrolling STI FS 4b52d62ee893d3b6: _116.
// material 1:1 = tiling.xy @0, speed.xy @8, reflectance @16.
inline AmUv am_scroll(AmUv uv,AmUv tiling,AmUv speed,float time){
 return am_uv(uv.x*tiling.x+speed.x*time,uv.y*tiling.y+speed.y*time);
}
// ENV_Lava_AlphaTest_Flowmap_Bounce_VT STI FS 8e6584bfaa46d6f3:
// _124, _131, _140. Flow texture at uv*m1, sampled with native 0:7.
// Unlike water's two-phase flow, this is alpha-weighted additive distortion.
inline AmUv am_lava_flow(AmUv uv,AmUv speed,float time,AmUv flow,float alpha,float strength){
 return am_uv(uv.x+(speed.x*time+(flow.x-.5f)*strength)*alpha,
              uv.y+(speed.y*time+(flow.y-.5f)*strength)*alpha);
}
// Same template VS 88d26a9941a51911 _138/_144. The caller supplies
// length(instance translation + camera origin) + objectY/wavelength + COLOR.B.
// WPO is along the native world N, weighted by COLOR.G and optional noise.
inline float am_lava_bounce(float time,float speed,float phase,float amplitude){
 float x=time*speed+phase+.5f;
 x=2.f*(x-am_floor(x))-1.f;x=x<0?-x:x;
 return x*x*(3.f-2.f*x)*amplitude;
}
// VFX_Model_...BillboardCamera_Flipbook_GradientMap_01 STI VS _360.._370.
// Native per-instance hash supplies phase and rate; do not replace it with a
// global frame counter. The atlas uses truncating remainder (not floor/frac).
inline float am_sprite_frame(float time,float rate,float phase,float columns,float rows){
 float frames=columns*rows;
 return rate*time*128.f+(phase-frames*am_trunc(phase/frames))+.00001f;
}
inline AmUv am_sprite_uv(AmUv uv,float frame,float columns,float rows){
 float x=am_trunc(frame-columns*am_trunc(frame/columns));
 float y=am_trunc(frame/columns-rows*am_trunc((frame/columns)/rows));
 return am_uv((uv.x+x)/columns,(uv.y+y)/rows);
}
#ifdef __cplusplus
}
#endif
#endif
