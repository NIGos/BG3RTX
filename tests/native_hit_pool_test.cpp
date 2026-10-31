// Pins the pool material record policy (common/native_hit_pool.h).
#include "../common/native_hit_pool.h"
#include <cstdio>
#include <cstdlib>
using namespace bg3::native_hit;
using namespace bg3::native_hit::pool;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Payload { bool alive = true; };
static auto alive = [](const Payload& p) { return p.alive; };
int main() {
  // Recipe: only the STI pool pair, never the audited per-draw pair.
  CHECK(pool_recipe(0x2d2ac3c38992b7f0ull, 0x14678345c10aefbcull) != nullptr);
  CHECK(pool_recipe(0x3812bc980438747full, 0x8995da1e4060f53bull) == nullptr);
  CHECK(pool_recipe(0x2d2ac3c38992b7f0ull, 0x8995da1e4060f53bull) == nullptr);
  CHECK(pool_recipe(0xa618ddae00000000ull, 0x14678345c10aefbcull) == nullptr);  // other class A VS: not audited
  CHECK(engine_bytes(*pool_recipe(0x2d2ac3c38992b7f0ull, 0x14678345c10aefbcull)) == 36);
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
  // Eviction drops the record.
  t.evicted(key);CHECK(t.find(key)==nullptr);CHECK(t.totals().evicted==1);CHECK(t.lookup(key,8,pos,90,1,alive)==Status::NoDeferred);
  t.evicted(key);CHECK(t.totals().evicted==1);
  // Capacity: overflow counted, nothing stored.
  Table<Payload> full;
  for (uint64_t k = 1; k <= Table<Payload>::kMaxRecords; ++k) CHECK(full.observe(k,1,q)==Table<Payload>::Store::Fresh);
  CHECK(full.observe(0xffffffffull,1,q)==Table<Payload>::Store::Full);CHECK(full.totals().overflow==1);CHECK(full.size()==Table<Payload>::kMaxRecords);
  std::printf("native_hit_pool_test: %u checks passed\n", checks);
  return 0;
}
