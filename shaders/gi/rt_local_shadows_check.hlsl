// D-LS bit-exactness check (ls_check=1): the live pass (rt_local_shadows.hlsl) and the
// frozen reference (ls_ref/ls_ref_body.hlsli, tools/lsref/make_ls_ref.py) on the same
// pixel, frame and inputs. Writes the live result; counts into rtStats (core/gi.cpp):
//   80 px the reference writes, 81 px whose outputs differ in any bit,
//   82 ... by more than 1e-5 relative, 83 px written by only one of them.
// ls_check=2 (pc.ls_pass bit 1) compares the reference with itself (a second evaluation at an
// index the compiler cannot prove equal): what differs then is not the code but the inputs
// changing under the pass (e.g. texture streaming), the floor of the check.
#define LS_NO_MAIN 1
#include "rt_local_shadows.hlsl"

#include "ls_ref/ls_ref_body.hlsli"

float ls_rel(float3 a, float3 b) {
  float3 d = abs(a - b);
  return max(max(d.x, d.y), d.z) / max(max(max(abs(a.x), abs(a.y)), abs(a.z)), 1e-20);
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  const uint2 fp = tid.xy;
  float3 ad = 0, as_ = 0, bd = 0, bs = 0;
  const bool wa = lsref_ls_ref(fp, ad, as_);
  const bool wb = (pc.ls_pass & 2u) != 0u ? lsref_ls_ref(WaveReadLaneAt(fp, WaveGetLaneIndex()), bd, bs)
                                          : ls_main(fp, bd, bs);
  const bool bits = wa && wb && (any(asuint(ad) != asuint(bd)) || any(asuint(as_) != asuint(bs)));
  const bool rel = bits && max(ls_rel(ad, bd), ls_rel(as_, bs)) > 1e-5;
  const uint bin = (fp.x >> 3) * 7 + (fp.y >> 3) * 13;
  stat_add(80, wa, bin);
  stat_add(81, bits || wa != wb, bin);
  stat_add(82, rel, bin);
  stat_add(83, wa != wb, bin);
  if (wb) {
    gDiffuse[fp] = bd;
    gSpecular[fp] = bs;
  }
}
