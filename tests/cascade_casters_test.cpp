// common/cascade_casters.h: cascade pass detection, the camera/cascade dedupe key, the
// orthographic clip -> world inversion (CPU mirror of xfb_to_world.hlsl XFB_ORTHO), the
// rebase translation and the fragment-discard scan.
#include "../common/cascade_casters.h"
#include "../layer/spirv_xfb.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

using namespace cascade;

static int fail(int code, const char* what) {
  std::printf("cascade_casters_test: FAIL %d %s\n", code, what);
  return code;
}

// Forward: what the cascade VS writes for camera-relative world W (y negated).
static void project(const float m[4][4], const float w[3], float clip[4]) {
  for (int j = 0; j < 4; ++j) clip[j] = w[0] * m[0][j] + w[1] * m[1][j] + w[2] * m[2][j] + m[3][j];
  clip[1] = -clip[1];
}

int main(int argc, char** argv) {
  // Optional native fixture check: static cascade VS, skinned cascade VS, wind
  // VS. No game files are required by CTest; supplied dumps are read-only.
  if(argc!=1 && argc!=4) return fail(85,"expected static, skin and wind SPIR-V paths");
  for(int i=1;i<argc;++i) {
    std::ifstream f(argv[i],std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(f)),{});
    if(bytes.size()<20 || bytes.size()%4) return fail(86,"native fixture unreadable");
    std::vector<uint32_t> words(bytes.size()/4);
    std::memcpy(words.data(),bytes.data(),bytes.size());
    const bool affine=bg3::analyze_vs_instancing(words.data(),words.size(),-1).ok;
    if(static_candidate(affine,true,false)!=(i==1)) return fail(87,"native static/skin/wind classification");
    std::printf("native fixture %s: affine %d\n",argv[i],int(affine));
  }
  // Static roof class A versus skin/cloth/wind, compute-skinned vertices and
  // unknown bindings. Key OFF preserves all legacy outcomes.
  for(bool affine : {false,true}) for(bool known : {false,true}) for(bool storage : {false,true}) {
    const bool candidate=static_candidate(affine,known,storage);
    if(candidate!=(affine && known && !storage)) return fail(70,"static classification");
    if(!keep_static_only(false,candidate)) return fail(71,"key off preserves legacy");
    if(keep_static_only(true,candidate)!=candidate) return fail(72,"key on rejects unknown/deformed");
  }
  SetChanges changes;
  if(changes.observe(10,false,{1,2})) return fail(73,"first set seeds");
  if(changes.observe(11,false,{2,1})) return fail(74,"order does not change membership");
  if(!changes.observe(12,false,{1,3})) return fail(75,"equal count replacement detected");
  if(!changes.observe(13,false,{})) return fail(76,"empty set transition");
  if(changes.observe(14,true,{1})) return fail(77,"policy toggle reseeds");
  if(changes.observe(20,true,{2})) return fail(78,"capture gap reseeds");
  if(changes.observe(19,true,{3})) return fail(79,"late recording ignored");
  if(changes.observe(21,true,{2})) return fail(80,"late recording did not replace newer set");
  // The old opaque stream must retain EXACTLY its legacy policy even when
  // the current material stream also records cards the old path skipped.
  for(int mode=0;mode<=2;++mode) {
    if(legacy_caster(false,mode,false,true,false,false)!=LegacyCaster::keep) return fail(60,"opaque retained");
    if(legacy_caster(false,mode,false,true,true,false)!=LegacyCaster::keep) return fail(61,"opaque ignores alpha mesh dedupe");
    if(legacy_caster(false,mode,false,true,false,true)!=LegacyCaster::duplicate) return fail(62,"placed duplicate removed");
  }
  if(legacy_caster(true,0,false,false,false,false)!=LegacyCaster::alpha_skip) return fail(63,"default unknown discard stays skipped");
  if(legacy_caster(true,0,true,false,false,false)!=LegacyCaster::alpha_skip) return fail(64,"default cutout stays skipped");
  if(legacy_caster(true,1,true,false,false,false)!=LegacyCaster::keep) return fail(65,"explicit solid experiment retained");
  if(legacy_caster(true,2,true,false,false,false)!=LegacyCaster::alpha_skip) return fail(66,"texture cutout never solid in mode2");
  if(legacy_caster(true,2,false,false,true,false)!=LegacyCaster::keep) return fail(67,"scalar mode2 allowed without dedupe");
  if(legacy_caster(true,2,false,true,true,false)!=LegacyCaster::duplicate) return fail(68,"scalar duplicate removed");
  // ---- pass detection ----
  if (pass_layer(0, true, 2048, 2048, 4, 1) != 1) return fail(1, "cascade layer 1");
  if (pass_layer(0, true, 2048, 2048, 4, 0) != 0) return fail(2, "cascade layer 0");
  if (pass_layer(1, true, 2048, 2048, 4, 1) != -1) return fail(3, "colour attachment");
  if (pass_layer(0, false, 2048, 2048, 4, 1) != -1) return fail(4, "not D16");
  if (pass_layer(0, true, 2560, 1067, 4, 1) != -1) return fail(5, "not square");
  if (pass_layer(0, true, 2048, 2048, 2, 1) != -1) return fail(6, "far cache (2 layers)");
  if (pass_layer(0, true, 512, 512, 4, 1) != -1) return fail(7, "too small");
  if (pass_layer(0, true, 2048, 2048, 4, 4) != -1) return fail(8, "layer out of range");

  // ---- dedupe key ----
  DrawId a;
  a.kind = 1, a.count = 300, a.instances = 4, a.first = 12, a.vertex_offset = 7;
  a.vb = 0x1000, a.vb_offset = 64, a.ib = 0x2000, a.ib_offset = 128, a.ib_type = 1;
  a.place = 0x3000, a.place_offset = 256;
  DrawId b = a;
  if (draw_key(a) != draw_key(b)) return fail(10, "equal draws, equal keys");
  b.place_offset = 512;   // the same mesh placed elsewhere (another instance stream range)
  if (draw_key(a) == draw_key(b)) return fail(11, "placement must count");
  // Reproduce the dialogue failure: the same opaque mesh and range have
  // distinct camera/shadow placement offsets. Alpha mesh dedupe cannot save
  // this skinned copy; static-only must remove it despite the draw-key miss.
  KeySets dialogue_camera;
  dialogue_camera.insert(100,draw_key(a));
  const auto old_character=legacy_caster(false,2,false,true,mesh_key(a)==mesh_key(b),
    dialogue_camera.contains(100,draw_key(b)));
  if(old_character!=LegacyCaster::keep) return fail(81,"regression must reproduce undeduped character");
  if(keep_static_only(true,static_candidate(false,true,false))) return fail(82,"lagged skin copy excluded");
  if(!keep_static_only(false,static_candidate(false,true,false))) return fail(83,"off retains old skin behavior");
  if(!keep_static_only(true,static_candidate(true,true,false))) return fail(84,"offscreen class A roof retained");
  // Default-path regression: two pass-local UBO allocations refer to the same
  // model and pulled vertex instance. No static-only filter needed.
  uint32_t model[16]={};model[0]=model[5]=model[10]=model[15]=0x3f800000u;
  a.instance=model_instance_key(model,120,0x9000,128);
  const float zero_origin[3]={};
  if(!model_position(a,model,zero_origin)) return fail(102,"finite model placement");
  b=a;b.place_offset+=256;
  InstanceSets camera_instances;
  camera_instances.insert(100,a);
  auto duplicate=[&](uint64_t frame,const DrawId& id) {
    return camera_instances.contains(frame,id);
  };
  if(!duplicate(100,b) || draw_key(a)==draw_key(b)) return fail(88,"offset-mismatched opaque duplicate removed");
  if(legacy_caster(false,0,false,true,false,duplicate(100,b))!=LegacyCaster::duplicate)
    return fail(89,"default opaque policy drops canonical duplicate");
  b.place=0x7777; // even a different ring allocation has identical contents
  if(!duplicate(100,b)) return fail(90,"pass-local allocation ignored");
  if(duplicate(101,b)) return fail(91,"previous frame cannot remove an offscreen caster");
  b=a;model[12]=0x41200000u; // identical prop translated by 10 metres
  b.instance=model_instance_key(model,120,0x9000,128);
  model_position(b,model,zero_origin);
  if(duplicate(100,b)) return fail(93,"different prop transform kept");
  model[12]=0;b=a;b.instance=model_instance_key(model,121,0x9000,128);
  if(duplicate(100,b)) return fail(94,"different pulled character at same transform kept");
  b=a;b.instance=model_instance_key(model,120,0x9001,128);
  if(duplicate(100,b)) return fail(95,"different pulled stream kept");
  b=a;b.instance=model_instance_key(model,120,0x9000,256);
  if(duplicate(100,b)) return fail(96,"different pulled range kept");
  b=a;b.first_instance++;
  if(duplicate(100,b)) return fail(97,"different instance range kept");
  b=a;b.instance=0;
  if(duplicate(100,b)) return fail(98,"unknown placement kept");
  b=a;b.vb++;
  if(duplicate(100,b)) return fail(100,"cascade-only roof mesh kept");
  if(model_pull_offset(0x1cf36b97d3067b6cull)!=64 || model_pull_offset(0x3812bc980438747full)!=96 ||
     model_pull_offset(0x50fbfceb06192473ull)!=0 || model_pull_offset(0x646a930e41f002bbull)!=0)
    return fail(101,"audited skin/camera layouts; wind and instanced roof remain exact");
  // Real draw shape: camera/cascade constants AND material UBOs are separate
  // ring allocations, and compute skinning may output into another VB region.
  // Reproduce the previous key exactly to prove the allocation miss first.
  auto previous_key=[](const DrawId& d,uint64_t material_buffer,uint64_t material_offset) {
    const uint64_t material=mix(mix(mix(1469598103934665603ull,material_buffer),material_offset),256);
    return mix(mix(mix(mix(mesh_key(d),material),d.instance),d.instances),d.first_instance);
  };
  DrawId camera_draw=a,cascade_draw=a;
  cascade_draw.place=0x8000;cascade_draw.place_offset=8192;
  if(previous_key(camera_draw,0x5000,256)==previous_key(cascade_draw,0x6000,4096))
    return fail(110,"previous key must miss per-draw material allocations");
  if(mesh_instance_key(camera_draw)!=mesh_instance_key(cascade_draw) || !duplicate(100,cascade_draw))
    return fail(111,"geometry identity ignores constant and material allocations");
  cascade_draw.vb=0x7777;cascade_draw.vb_offset=16384;
  if(duplicate(100,cascade_draw)) return fail(112,"different skin output cannot prove common source");
  // A camera VS outside the audited matrix layouts cannot supply placement.
  camera_draw.instance=0;camera_draw.position_known=false;
  if(mesh_instance_key(camera_draw)!=0 || model_pull_offset(0x0123456789abcdefull)!=0)
    return fail(113,"unlisted camera layout has key zero");
  InstanceSets unknown_camera;unknown_camera.insert(100,camera_draw);
  if(unknown_camera.contains(100,cascade_draw)) return fail(114,"unknown camera cannot dedupe skin");
  for(bool affine : {false,true}) {
    // Skin VS is non-affine; compute-skinned affine input is storage-backed.
    const bool skin=static_candidate(affine,true,true);
    if(keep_static_only(true,skin)) return fail(115,"fallback removes both skin paths");
    if(!keep_static_only(false,skin)) return fail(116,"fallback off retains comparison path");
  }
  DrawId prop=cascade_draw;prop.position[0]+=10;
  InstanceSets props;props.insert(100,cascade_draw);
  if(props.contains(100,prop)) return fail(117,"identical prop geometry at another place remains distinct");
  // Different pass origins (as with a distant sun-cascade eye) still identify
  // the same absolute instance. Translation precision comes from input ULPs.
  DrawId camera=a,shadow=a;
  const float camera_origin[3]={1000.f,20.f,-400.f},shadow_origin[3]={1092.f,-10.f,-320.f};
  const float camera_relative[3]={2.25f,-1.5f,3.f},shadow_relative[3]={-89.75f,28.5f,-77.f};
  for(unsigned k=0;k<3;++k) std::memcpy(model+12+k,camera_relative+k,4);
  model_position(camera,model,camera_origin);
  camera.instance=model_instance_key(model,120,0x9000,128);
  for(unsigned k=0;k<3;++k) std::memcpy(model+12+k,shadow_relative+k,4);
  model_position(shadow,model,shadow_origin);
  shadow.instance=model_instance_key(model,120,0x9000,128);
  InstanceSets origin_instances;origin_instances.insert(200,camera);
  if(!origin_instances.contains(200,shadow)) return fail(103,"different origins same absolute placement");
  if(origin_instances.contains(201,shadow)) return fail(104,"normalized placement remains same-frame only");
  shadow.position[0]+=1.0;
  if(origin_instances.contains(200,shadow)) return fail(105,"different absolute prop placement kept");
  // An absolute coordinate close to a float boundary can round differently
  // after changing origins. Both rounded input intervals contain the source.
  const double absolute=1000000.04;
  const float origin_a[3]={1000000.f,0,0},origin_b[3]={0,0,0};
  const float relative_a=float(absolute-double(origin_a[0])),relative_b=float(absolute);
  model[12]=model[13]=model[14]=0;
  std::memcpy(model+12,&relative_a,4);model_position(camera,model,origin_a);
  camera.instance=model_instance_key(model,120,0x9000,128);
  std::memcpy(model+12,&relative_b,4);model_position(shadow,model,origin_b);
  shadow.instance=model_instance_key(model,120,0x9000,128);
  InstanceSets rounded;rounded.insert(300,camera);
  if(camera.position[0]==shadow.position[0] || !rounded.contains(300,shadow))
    return fail(106,"derived float roundoff admits common-source boundary rounding");
  shadow.position[0]=camera.position[0]+2*(camera.position_error[0]+shadow.position_error[0]);
  if(rounded.contains(300,shadow)) return fail(107,"placement outside derived precision kept");
  const float bad_origin[3]={INFINITY,0,0};
  if(model_position(shadow,model,bad_origin) || rounded.contains(300,shadow)) return fail(108,"nonfinite origin stays unknown");
  model[0]=0x7fc00000u;
  if(model_position(shadow,model,zero_origin)) return fail(109,"nonfinite model stays unknown");
  b = a, b.instances = 5;
  if (draw_key(a) == draw_key(b)) return fail(12, "instance count must count");
  b = a, b.ib_offset = 0;
  if (draw_key(a) == draw_key(b)) return fail(13, "index range must count");
  b = a, b.vertex_offset = 0;
  if (draw_key(a) == draw_key(b)) return fail(14, "vertex offset must count");
  b = a, b.kind = 0;
  DrawId c = b;
  c.ib = 0x9999;   // non-indexed: the bound index buffer is irrelevant
  if (draw_key(b) != draw_key(c)) return fail(15, "non-indexed ignores the index buffer");

  KeySets ks;
  ks.insert(100, 1), ks.insert(100, 2);
  if (!ks.contains(100, 1) || !ks.contains(101, 2)) return fail(20, "this frame and the next see the keys");
  if (ks.contains(102, 1)) return fail(21, "two frames later: stale");
  ks.insert(101, 3);
  if (!ks.contains(101, 1) || !ks.contains(101, 3)) return fail(22, "union of two frames");
  ks.insert(102, 4);   // reuses frame 100's set
  if (ks.contains(102, 1) || !ks.contains(102, 3) || !ks.contains(102, 4)) return fail(23, "ring of two");
  if (ks.size(102) != 1) return fail(24, "fresh set per frame");

  // ---- orthographic inversion ----
  // A sun cascade: orthonormal light axes, 60 m x 60 m x 400 m deep, off-centre.
  const float s = 0.70710678f;
  const float ax[3] = {s, 0, -s}, ay[3] = {0.40824829f, 0.81649658f, 0.40824829f},
              az[3] = {0.57735027f, -0.57735027f, 0.57735027f};
  const float ex = 60, ey = 60, ez = 400;
  float m[4][4] = {};
  for (int k = 0; k < 3; ++k) {
    m[k][0] = ax[k] * 2 / ex;
    m[k][1] = ay[k] * 2 / ey;
    m[k][2] = az[k] / ez;
    m[k][3] = 0;
  }
  m[3][0] = 0.1f, m[3][1] = -0.2f, m[3][2] = 0.5f, m[3][3] = 1;
  Ortho o = ortho_check(m);
  if (!o.ok) return fail(30, "orthographic matrix accepted");
  if (std::fabs(o.extent[0] - ex) > 1e-3f || std::fabs(o.extent[1] - ey) > 1e-3f || std::fabs(o.extent[2] - ez) > 1e-2f)
    return fail(31, "extents");
  const float pts[4][3] = {{0, 0, 0}, {12.5f, -3.25f, 7}, {-25, 40, -18.5f}, {150.f, 2.f, -90.f}};
  for (const auto& w : pts) {
    float clip[4], back[3];
    project(m, w, clip);
    if (!clip_to_world(m, clip, back)) return fail(32, "valid vertex rejected");
    for (int k = 0; k < 3; ++k)
      if (std::fabs(back[k] - w[k]) > 1e-3f * (1.0f + std::fabs(w[k]))) return fail(33, "round trip");
  }
  {
    float clip[4], back[3];
    project(m, pts[1], clip);
    clip[3] = 0.5f;   // not this matrix's output (perspective w)
    if (clip_to_world(m, clip, back)) return fail(34, "wrong w accepted");
    clip[3] = NAN;
    if (clip_to_world(m, clip, back)) return fail(35, "NaN accepted");
  }
  float persp[4][4] = {};
  std::memcpy(persp, m, sizeof m);
  persp[2][3] = -1, persp[3][3] = 0;   // a perspective w row
  if (ortho_check(persp).ok) return fail(36, "perspective matrix rejected");
  float flat[4][4] = {};
  flat[3][3] = 1;
  if (ortho_check(flat).ok) return fail(37, "singular matrix rejected");

  // ---- rebase ----
  const float prev[3] = {100.f, 5.f, -40.f}, cur[3] = {101.5f, 5.f, -39.f};
  float t[3];
  rebase(prev, cur, t);
  // A world point P: relative to prev it was P - prev; translated, it must be P - cur.
  const float P[3] = {120.f, 9.f, -30.f};
  for (int k = 0; k < 3; ++k)
    if (std::fabs((P[k] - prev[k]) + t[k] - (P[k] - cur[k])) > 1e-5f) return fail(40, "rebase translation");

  // ---- fragment discard scan ----
  // Minimal modules: header, OpEntryPoint <model> %1 "m", then the opcode under test.
  auto module = [](uint32_t model, uint32_t op) {
    std::vector<uint32_t> w = {0x07230203u, 0x00010000u, 0, 10, 0};
    w.push_back((4u << 16) | 15u), w.push_back(model), w.push_back(1), w.push_back(0x6d);   // OpEntryPoint
    if (op) w.push_back((1u << 16) | op);
    return w;
  };
  auto fs_kill = module(4, 252), fs_demote = module(4, 5380), fs_term = module(4, 4416), fs_none = module(4, 0),
       vs_kill = module(0, 252);
  if (!spirv_fragment_discards(fs_kill.data(), fs_kill.size())) return fail(50, "OpKill");
  if (!spirv_fragment_discards(fs_demote.data(), fs_demote.size())) return fail(51, "OpDemoteToHelperInvocation");
  if (!spirv_fragment_discards(fs_term.data(), fs_term.size())) return fail(52, "OpTerminateInvocation");
  if (spirv_fragment_discards(fs_none.data(), fs_none.size())) return fail(53, "empty fragment shader");
  if (spirv_fragment_discards(vs_kill.data(), vs_kill.size())) return fail(54, "not a fragment shader");
  uint32_t junk[6] = {0x07230203u, 0, 0, 0, 0, 0};   // a zero-length instruction ends the scan
  if (spirv_fragment_discards(junk, 6)) return fail(55, "malformed module");

  std::printf("cascade_casters_test: ok\n");
  return 0;
}
