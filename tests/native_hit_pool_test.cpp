// Pins the pool material record policy (common/native_hit_pool.h).
#include "../common/native_hit_pool.h"
#include "../common/native_hit_join.h"
#include "../layer/spirv_xfb.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
using namespace bg3::native_hit;
using namespace bg3::native_hit::pool;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Payload { bool alive = true; };
static auto alive = [](const Payload& p) { return p.alive; };
// The STI G-buffer VS output interface (2d2ac3c3: vec2 UV at location 0, vec3 N/B/T at 1/2/3)
// gets the native VT frame patch: UV at 16, locations 1/2/3 at 24/36/48 with the XFB stride.
static bool sti_patch(const std::vector<uint32_t>& code, uint32_t stride) {
  std::vector<uint32_t> patched, off, plain;int uv = -1;uint32_t hair = 0, vt = 0;
  if (!bg3::patch_vs(code.data(), code.size(), patched, stride, &uv, &hair, true, &vt)) return false;
  if (uv != 0 || hair != 0 || vt != 1) return false;
  std::vector<uint32_t> outputs;uint32_t loc[4]{};
  for (size_t i = 5; i < code.size(); i += code[i] >> 16) if ((code[i] & 65535) == 59 && (code[i] >> 16) >= 4 && code[i + 3] == 3) outputs.push_back(code[i + 2]);
  for (size_t i = 5; i < code.size(); i += code[i] >> 16)
    if ((code[i] & 65535) == 71 && (code[i] >> 16) == 4 && code[i + 2] == 30 && code[i + 3] < 4 &&
        std::find(outputs.begin(), outputs.end(), code[i + 1]) != outputs.end()) loc[code[i + 3]] = code[i + 1];
  for (auto [l, at] : {std::pair{0u, 16u}, std::pair{1u, 24u}, std::pair{2u, 36u}, std::pair{3u, 48u}}) {
    if (!loc[l]) return false;bool offset = false, step = false;
    for (size_t i = 5; i < patched.size(); i += patched[i] >> 16)
      if ((patched[i] & 65535) == 71 && (patched[i] >> 16) == 4 && patched[i + 1] == loc[l]) {
        offset |= patched[i + 2] == 35 && patched[i + 3] == at;step |= patched[i + 2] == 37 && patched[i + 3] == stride;
      }
    if (!offset || !step) return false;
  }
  // Without the flag (rt_hit_material off): patched exactly as before, without the frame.
  return bg3::patch_vs(code.data(), code.size(), off, stride) && bg3::patch_vs(code.data(), code.size(), plain, stride, nullptr, nullptr, false, nullptr) &&
         off == plain && off != patched;
}
int main(int argc, char** argv) {
  // Recipe: only the STI pool pair, never the audited per-draw pair.
  CHECK(pool_recipe(0x2d2ac3c38992b7f0ull, 0x14678345c10aefbcull) != nullptr);
  CHECK(pool_recipe(0x3812bc980438747full, 0x8995da1e4060f53bull) == nullptr);
  CHECK(pool_recipe(0x2d2ac3c38992b7f0ull, 0x8995da1e4060f53bull) == nullptr);
  CHECK(pool_recipe(0xa618ddae00000000ull, 0x14678345c10aefbcull) == nullptr);  // other class A VS: not audited
  // FS 14678345 engine UBO (1:0): _m0[0] p0 at 0, _m0[1] p1 at 16, _m1 tile-set index at 32, _m2 channel at 36.
  const auto& sti = *pool_recipe(0x2d2ac3c38992b7f0ull, 0x14678345c10aefbcull);
  CHECK(sti.transform_offset == 0 && sti.tileset_index_offset == 32 && sti.channel_offset == 36);
  CHECK(engine_bytes(sti) == 40);
  CHECK(engine_bytes(*bg3::native_vt::recipe(0x8995da1e4060f53bull)) == 108);
  // Reflectance variants (audit of the dumped GLSL, PLAN_M4 "next families"): same VS, same
  // engine UBO layout and sampling as their base FS; only RT2.y differs.
  namespace vt = bg3::native_vt;
  const auto* half = pool_recipe(0x2d2ac3c38992b7f0ull, 0x3df678c09f13ea60ull);
  const auto* mixed = pool_recipe(0x2d2ac3c38992b7f0ull, 0x8165785442a20e57ull);
  CHECK(half && mixed && half != &sti && mixed != &sti && half != mixed);
  for (const auto* r : {half, mixed})
    CHECK(r->transform_offset == sti.transform_offset && r->tileset_index_offset == sti.tileset_index_offset &&
          r->channel_offset == sti.channel_offset && engine_bytes(*r) == 40);
  CHECK(sti.reflectance == vt::ReflectanceUbo && half->reflectance == vt::ReflectanceConstant && mixed->reflectance == vt::ReflectanceNormalMix);
  CHECK(pool_recipe(0xa618ddae00000000ull, 0x3df678c09f13ea60ull) == nullptr);
  // The per-draw variant 3812bc98/3da860be: never a pool pair, an audited per-draw pair.
  CHECK(pool_recipe(0x3812bc980438747full, 0x3da860bebbe9133bull) == nullptr);
  CHECK(audited_static_family(0x3812bc980438747full, 0x3da860bebbe9133bull));
  CHECK(audited_static_family(0x3812bc980438747full, 0x8995da1e4060f53bull));
  CHECK(!audited_static_family(0x2d2ac3c38992b7f0ull, 0x3da860bebbe9133bull));
  CHECK(!audited_static_family(0x3812bc980438747full, 0x14678345c10aefbcull));
  const auto* per_draw = vt::recipe(0x3da860bebbe9133bull);
  const auto* base = vt::recipe(0x8995da1e4060f53bull);
  CHECK(per_draw && per_draw->reflectance == vt::ReflectanceConstant && per_draw->transform_offset == base->transform_offset &&
        per_draw->tileset_index_offset == base->tileset_index_offset && per_draw->channel_offset == base->channel_offset);
  // nv_reflectance: UBO value, constant 0.5, mix(0.5, n.x, p) (GLSL mix = a + (b - a) t).
  using bg3::transport::nv_reflectance;
  CHECK(nv_reflectance(vt::ReflectanceUbo, 0.25f, 0.9f) == 0.25f);
  CHECK(nv_reflectance(vt::ReflectanceConstant, 0.25f, 0.9f) == 0.5f);
  CHECK(nv_reflectance(vt::ReflectanceNormalMix, 0.f, 0.9f) == 0.5f);
  CHECK(nv_reflectance(vt::ReflectanceNormalMix, 1.f, 0.9f) == 0.9f);
  CHECK(std::fabs(nv_reflectance(vt::ReflectanceNormalMix, 0.25f, 0.9f) - 0.6f) < 1e-6f);
  // Pipeline rule: exact_material_pipeline without the XFB/basis/UV terms.
  MaterialPipelineFacts f;f.material_state=true;f.depth_test=1;f.color_mask=0xf73u;f.triangle_list=true;
  CHECK(pool_material_pipeline(f));
  for (unsigned bits = 0; bits < 64; ++bits) {
    MaterialPipelineFacts g;g.material_state=bits&1;g.depth_test=(bits&2)?1:((bits&32)?-1:0);g.blend_mask=(bits&4)?1:0;
    g.color_mask=(bits&8)?0xf73u:0x073u;g.triangle_list=bits&16;
    const bool want=g.material_state && g.depth_test==1 && g.blend_mask==0 && g.color_mask==0xf73u && g.triangle_list;
    CHECK(pool_material_pipeline(g)==want);
    g.xfb=false;g.vt_outputs=0;g.uv_ok=false;CHECK(pool_material_pipeline(g)==want);  // no XFB terms
  }
  // Geometry key: indexed draws by index range, independent of the position stream; the
  // position stream has its own key.
  Geometry a;a.indexed=true;a.count=300;a.first=12;a.vertex_offset=40;a.ib=0x1000;a.ib_offset=64;a.ib_type=1;a.position_buffer=0x2000;a.position_offset=0;
  Geometry b=a;b.position_buffer=0x3000;
  CHECK(geometry_key(a)==geometry_key(b));CHECK(position_key(a)!=position_key(b));
  b=a;b.first=15;CHECK(geometry_key(a)!=geometry_key(b));
  b=a;b.vertex_offset=-1;CHECK(geometry_key(a)!=geometry_key(b));
  b=a;b.ib_offset=128;CHECK(geometry_key(a)!=geometry_key(b));
  b=a;b.indexed=false;CHECK(geometry_key(a)!=geometry_key(b));
  Geometry c=a;c.indexed=false;Geometry d=c;d.position_offset=16;CHECK(geometry_key(c)!=geometry_key(d));  // non-indexed: by stream
  b=a;b.position_buffer=0;CHECK(position_key(b)==0);CHECK(position_key(a)!=0);CHECK(geometry_key(a)!=0);
  const uint64_t key=geometry_key(a),pos=position_key(a);
  // Table: no record, then a qualified record for one mesh.
  Table<Payload> t;
  CHECK(t.lookup(key,7,pos,10,3,alive)==Status::NoDeferred);
  Observation q{0xabc,pos,Status::Qualified};
  CHECK(t.observe(key,10,q)==Table<Payload>::Store::Fresh);
  CHECK(t.observe(key,10,q)==Table<Payload>::Store::Keep);  // same material twice in one frame: no conflict
  CHECK(t.lookup(key,7,pos,10,3,alive)==Status::Qualified);
  CHECK(t.counters().instances[size_t(Status::Qualified)]==3);CHECK(t.counters().instances[size_t(Status::NoDeferred)]==3);
  CHECK(t.counters().draws[size_t(Status::Qualified)]==1);
  // Persistent: still qualified in later frames without a new observation.
  t.end_frame();CHECK(t.counters().draws[size_t(Status::Qualified)]==0);
  CHECK(t.lookup(key,7,pos,50,1,alive)==Status::Qualified);
  // Different position stream: declined.
  CHECK(t.lookup(key,7,pos^2,50,1,alive)==Status::Position);
  CHECK(t.lookup(key,7,0,50,1,alive)==Status::Position);
  // Stale resources: counted once per invalidation, cleared by a fresh observation.
  t.find(key)->payload.alive=false;
  CHECK(t.lookup(key,7,pos,51,1,alive)==Status::Stale);CHECK(t.lookup(key,7,pos,52,1,alive)==Status::Stale);
  CHECK(t.totals().stale==1);
  CHECK(t.observe(key,53,q)==Table<Payload>::Store::Fresh);t.find(key)->payload.alive=true;
  CHECK(t.lookup(key,7,pos,53,1,alive)==Status::Qualified);CHECK(t.totals().refreshed==1);
  // Mesh replaced: declined until an observation at or after the change.
  CHECK(t.lookup(key,8,pos,60,1,alive)==Status::Replaced);CHECK(t.totals().replaced==1);
  CHECK(t.lookup(key,8,pos,61,1,alive)==Status::Replaced);
  t.observe(key,61,q);CHECK(t.lookup(key,8,pos,61,1,alive)==Status::Qualified);
  // Two materials for one geometry in one frame: ambiguous in that frame and the next observed frame.
  t.observe(key,70,q);Observation other{0xdef,pos,Status::Qualified};
  CHECK(t.observe(key,70,other)==Table<Payload>::Store::Keep);CHECK(t.totals().conflicts==1);
  CHECK(t.lookup(key,8,pos,70,1,alive)==Status::Ambiguous);
  t.observe(key,71,q);CHECK(t.lookup(key,8,pos,71,1,alive)==Status::Ambiguous);
  t.observe(key,72,q);CHECK(t.lookup(key,8,pos,72,1,alive)==Status::Qualified);
  // A pair/status mismatch in one frame is a conflict too; declined statuses pass through.
  const uint64_t key2=geometry_key(b);
  t.observe(key2,80,{0x11,pos,Status::Family});CHECK(t.lookup(key2,9,pos,80,2,alive)==Status::Family);
  t.observe(key2,81,{0x11,pos,Status::Binding});CHECK(t.lookup(key2,9,pos,81,2,alive)==Status::Binding);
  t.observe(key2,81,{0x11,pos,Status::Qualified});CHECK(t.lookup(key2,9,pos,81,2,alive)==Status::Ambiguous);
  // Capture throttle: a current qualified record is refreshed every kRefreshFrames; the
  // refresh frame itself, a stale, declined, ambiguous or replaced record capture at once.
  {
    Table<Payload> r;const uint64_t k=0x77;const uint64_t R=Table<Payload>::kRefreshFrames;
    CHECK(r.capture_due(k,100));r.observe(k,100,q);
    CHECK(r.capture_due(k,100));CHECK(!r.capture_due(k,101));CHECK(!r.capture_due(k,100+R-1));CHECK(r.capture_due(k,100+R));
    CHECK(r.lookup(k,1,pos,101,1,alive)==Status::Qualified);
    r.find(k)->payload.alive=false;CHECK(r.lookup(k,1,pos,102,1,alive)==Status::Stale);CHECK(r.capture_due(k,103));
    r.observe(k,103,q);r.find(k)->payload.alive=true;CHECK(!r.capture_due(k,104));
    CHECK(r.lookup(k,2,pos,105,1,alive)==Status::Replaced);CHECK(r.capture_due(k,106));
    r.observe(k,106,q);CHECK(!r.capture_due(k,107));
    r.observe(k,106,other);CHECK(r.capture_due(k,107));  // conflict
    r.observe(k,107,q);CHECK(r.capture_due(k,108));r.observe(k,108,q);CHECK(!r.capture_due(k,109));
    r.observe(k,140,{1,pos,Status::Binding});CHECK(r.capture_due(k,141));
  }
  // Eviction drops the record (its payload handed to the caller first: the slot release).
  {Table<Payload> e;e.observe(1,1,q);int seen=0;e.evicted(1,[&](Payload& p){seen+=p.alive;});CHECK(seen==1 && e.find(1)==nullptr);
   e.evicted(1,[&](Payload&){++seen;});CHECK(seen==1);int n=0;t.for_each([&](uint64_t,auto&){++n;});CHECK(n==int(t.size()));}
  t.evicted(key);CHECK(t.find(key)==nullptr);CHECK(t.totals().evicted==1);CHECK(t.lookup(key,8,pos,90,1,alive)==Status::NoDeferred);
  t.evicted(key);CHECK(t.totals().evicted==1);
  // Capacity: overflow counted, nothing stored.
  Table<Payload> full;
  for (uint64_t k = 1; k <= Table<Payload>::kMaxRecords; ++k) CHECK(full.observe(k,1,q)==Table<Payload>::Store::Fresh);
  CHECK(full.observe(0xffffffffull,1,q)==Table<Payload>::Store::Full);CHECK(full.totals().overflow==1);CHECK(full.size()==Table<Payload>::kMaxRecords);
  // UV/basis layout shared with the shaders (native_hit_records.h).
  CHECK(basis_bytes(0) == 16 && basis_bytes(1) == 16 + 132 && basis_bytes(1000) == 16 + 132000);
  CHECK(nh_pool_basis_vertex == 8 + 3 * 12 && nh_pool_basis_header == 16);
  CHECK(nh_pool_row_base == 16 + nh_material_rows * 256 + 16);  // after the publication word
  CHECK((nh_pool_meta & (nh_opaque_meta | (1u << 8) | (1u << 9) | (1u << 10) | (1u << 11) | (1u << 12) | (1u << 14) | 255u)) == 0);
  CHECK((nh_pool_rec_flag & (nh_pool_rows - 1)) == 0 && nh_pool_rec_flag >= (1u << 31));  // never a valid alpha record address
  CHECK(kBasisCapBytes / (3 * nh_pool_basis_vertex) >= 2000000);  // ~2M triangles of UV/basis (132 B each)
  // Basis state: due once, pending, then ready (ok) or failed with backoff; a new mesh starts over.
  {
    BasisTrack b;CHECK(basis_rebind(b,5)==UINT32_MAX && b.gen==5 && basis_due(b,10));
    basis_pending(b,10);CHECK(!basis_due(b,11));
    basis_done(b,BasisResult::Mismatch,20);CHECK(b.state==Basis::Failed && !basis_due(b,139) && basis_due(b,140));
    basis_pending(b,140);basis_done(b,BasisResult::Lost,150);CHECK(!basis_due(b,389) && basis_due(b,390));
    for(int i=0;i<10;++i){basis_pending(b,1);basis_done(b,BasisResult::Invalid,1000);}CHECK(b.since==1000+(120ull<<6));
    basis_pending(b,2000);basis_done(b,BasisResult::Ok,2001);CHECK(b.state==Basis::Ready && b.failures==0 && !basis_due(b,1u<<30));
    b.slot=7;CHECK(basis_rebind(b,5)==UINT32_MAX && b.slot==7);CHECK(basis_rebind(b,6)==7 && b.state==Basis::None && b.slot==UINT32_MAX && b.gen==6);
  }
  CHECK(basis_result(false,false,false,0,0)==BasisResult::Lost && basis_result(true,false,false,0,0)==BasisResult::Lost);
  CHECK(basis_result(true,true,true,0,0)==BasisResult::PoolMoved && basis_result(true,true,false,0,1)==BasisResult::Invalid);
  CHECK(basis_result(true,true,false,1.0f,0)==BasisResult::Ok && basis_result(true,true,false,1.001f,0)==BasisResult::Mismatch);
  // Slots: unique, reused after release, bounded by nh_pool_rows.
  {
    Slots s;std::set<uint32_t> got;for(uint32_t i=0;i<nh_pool_rows;++i){uint32_t x=s.acquire();CHECK(x<nh_pool_rows && got.insert(x).second);}
    CHECK(s.acquire()==UINT32_MAX && s.used()==nh_pool_rows);s.release(17);CHECK(s.used()==nh_pool_rows-1 && s.acquire()==17);
    s.release(UINT32_MAX);CHECK(s.used()==nh_pool_rows);s.clear();CHECK(s.used()==0 && s.acquire()==0);
  }
  // XFB patch audit on a minimal module with the 2d2ac3c3 interface (the dumped module: argv[1]).
  {
    std::vector<uint32_t> code={0x07230203,0x10000,0,100,0};
    auto emit=[&](std::initializer_list<uint32_t> v){code.insert(code.end(),v);};
    emit({(2u<<16)|17,1});emit({(5u<<16)|15,0,50,0x6e69616d,0});emit({(4u<<16)|71,20,11,0});
    for(uint32_t i=0;i<4;++i)emit({(4u<<16)|71,30+i,30,i});
    emit({(3u<<16)|22,1,32});emit({(4u<<16)|23,2,1,2});emit({(4u<<16)|23,3,1,3});emit({(4u<<16)|23,4,1,4});
    emit({(4u<<16)|32,5,3,2});emit({(4u<<16)|32,6,3,3});emit({(4u<<16)|32,7,3,4});emit({(4u<<16)|59,7,20,3});
    emit({(4u<<16)|59,5,30,3});for(uint32_t i=1;i<4;++i)emit({(4u<<16)|59,6,30+i,3});
    CHECK(sti_patch(code,64));CHECK(sti_patch(code,88));
    std::vector<uint32_t> narrow;uint32_t vt=9;CHECK(bg3::patch_vs(code.data(),code.size(),narrow,24,nullptr,nullptr,true,&vt) && vt==0);  // stride < 64: no frame
    auto flat=code;flat.insert(flat.end(),{(3u<<16)|71,32,14});uint32_t fl=9;std::vector<uint32_t> out;
    CHECK(bg3::patch_vs(flat.data(),flat.size(),out,64,nullptr,nullptr,true,&fl) && fl==0);  // a flat B is not the smooth frame
    if (argc >= 2) {
      std::ifstream in(argv[1], std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(in)), {});
      CHECK(bytes.size() >= 20 && bytes.size() % 4 == 0);std::vector<uint32_t> native(bytes.size() / 4);std::memcpy(native.data(), bytes.data(), bytes.size());
      CHECK(sti_patch(native, 64));CHECK(sti_patch(native, 88));
      if (argc >= 3) {std::vector<uint32_t> o;uint32_t v=0;CHECK(bg3::patch_vs(native.data(),native.size(),o,64,nullptr,nullptr,true,&v) && v==1);
        std::ofstream w(argv[2],std::ios::binary);w.write(reinterpret_cast<const char*>(o.data()),std::streamsize(o.size()*4));CHECK(bool(w));}
    }
  }
  std::printf("native_hit_pool_test: %u checks passed\n", checks);
  return 0;
}
