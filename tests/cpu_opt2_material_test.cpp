#include "../common/cpu_material_scratch.h"
#include "../common/native_hit_records.h"
#include "../common/native_hit_instanced.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>

static void check(bool ok){if(!ok)std::abort();}
struct Row {uint32_t record;uint64_t payload;};
struct View {uint64_t handle,id,image;uint32_t layout;bool operator==(const View&)const=default;};
static bool same(const View& a,const View& b){return a==b;}
int main(){
 using namespace bg3;
 // Actual production classification helper vs the former two-call expressions,
 // including a live control change across the mutex wait.
 std::vector<std::pair<uint64_t,uint64_t>> pairs{{0,0},{0x3812bc980438747full,0x8995da1e4060f53bull}};
 for(const auto& pair:native_hit::instanced::kPairs)pairs.emplace_back(pair.vs,pair.fs);
 for(const auto& [vs,fs]:pairs)
 for(bool tileset:{false,true})for(bool enabled:{false,true})for(bool wind:{false,true})
 for(bool exact:{false,true})for(uint32_t kind:{0u,1u,2u})for(uint32_t instances:{0u,1u,3u})for(uint32_t stride:{0u,63u,64u,80u}){
  const auto family=!tileset && enabled?native_hit::instanced::deferred_family(vs,fs,wind):native_hit::instanced::None;
  const auto* recipe=family!=native_hit::instanced::None?native_hit::instanced::recipe(vs,fs,wind):nullptr;
  const auto expected=recipe?native_hit::instanced::deferred_qualify(exact,kind,instances,stride):
   native_hit::deferred_qualify(native_hit::audited_static_family(vs,fs)||tileset||recipe,tileset,exact,kind,instances,stride);
  const auto actual=native_hit::instanced::classify_deferred(vs,fs,tileset,enabled,wind,exact,kind,instances,stride);
  check(actual.tileset==tileset && actual.family==family && actual.recipe==recipe && actual.result==expected);
  for(bool early_enabled:{false,true}){
   const auto early=native_hit::instanced::classify_deferred(vs,fs,tileset,early_enabled,wind,exact,kind,instances,stride);
   const auto after_wait=early_enabled==enabled?early:actual;
   check(after_wait.result==expected && after_wait.recipe==recipe && after_wait.family==family);
  }
 }
 cpu_opt::FirstRecordIndex<native_hit::nh_material_rows> index;
 std::vector<Row> rows{{0,1},{8192,2},{12287,3},{0,4},{UINT32_MAX,5},{12288,6}};
 std::mt19937 rng(42);
 for(uint32_t i=0;i<4096;++i)rows.push_back({rng()%12289u,rng()});
 auto compare=[&](uint32_t record){
  auto expected=std::find_if(rows.begin(),rows.end(),[&](const Row& r){return r.record==record;});
  const auto actual=index.find(rows,record);
  check(actual==expected);if(actual!=rows.end())check(actual->payload==expected->payload);
 };
 index.build(rows);for(uint32_t i=0;i<12300;++i)compare(i);compare(UINT32_MAX);
 // New source epoch, changed payload, missing/empty lists, duplicate first wins.
 rows={{4,9},{4,10},{7,11}};index.build(rows);compare(0);compare(4);compare(7);compare(8192);
 rows={{12287,13},{12287,14}};index.build(rows);compare(0);compare(12287);compare(12286);
 rows={{12288,15},{UINT32_MAX,16}};index.build(rows);compare(0);compare(12287);compare(12288);compare(UINT32_MAX);
 rows.clear();index.build(rows);compare(4);compare(UINT32_MAX);
 // Production table planner: compare commit order and rollback against the
 // previous vector algorithm for all capacities, aliases and changed identities.
 std::vector<View> candidates{{1,1,1,0},{1,1,1,0},{1,2,1,0},{2,2,2,1},{3,3,3,1},{2,2,2,1}};
 for(size_t cap=0;cap<=12;++cap)for(size_t prefix=0;prefix<=candidates.size();++prefix){
  std::vector<View> reference{{1,1,1,0}},actual=reference;
  std::vector<const View*> added;
  cpu_opt::UniqueAppend<View,12> fixed;
  for(size_t i=0;i<prefix;++i){const auto& v=candidates[i];
   if(!std::any_of(reference.begin(),reference.end(),[&](const auto& row){return same(row,v);}) &&
      !std::any_of(added.begin(),added.end(),[&](const auto* row){return same(*row,v);}))added.push_back(&v);
   fixed.need(v,actual,same);
  }
  const bool expected=reference.size()+added.size()<=cap;
  check(fixed.fits(actual.size(),cap)==expected);
  if(expected){for(auto v:added)reference.push_back(*v);for(auto v:fixed)actual.push_back(*v);}
  check(actual==reference); // failed material cannot consume any capacity
 }
 {cpu_opt::UniqueAppend<View,12> fixed;std::vector<View> empty,eleven;
  for(uint64_t i=0;i<11;++i)eleven.push_back({i,i,i,0});
  for(const auto& view:eleven)fixed.need(view,empty,same);
  check(fixed.size()==11 && !fixed.fits(0,10) && fixed.fits(0,11));}
 rows.reserve(4096);for(uint32_t i=0;i<4096;++i)rows.push_back({i,uint64_t(i)*37});
 auto bench=[&](bool optimized){uint64_t receipt=0;const auto t=std::chrono::steady_clock::now();
  for(int frame=0;frame<64;++frame){cpu_opt::FirstRecordIndex<native_hit::nh_material_rows> query_index;if(optimized)query_index.build(rows);
   for(uint32_t record=0;record<rows.size();++record){auto r=optimized?query_index.find(rows,record):std::find_if(rows.begin(),rows.end(),[&](const auto& row){return row.record==record;});receipt+=r->payload;}}
  return std::pair{std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count()/64,receipt};};
 for(uint32_t count:{4096u,512u,128u,32u}){rows.resize(count);const auto off=bench(false),on=bench(true);check(off.second==on.second);
  std::printf("cpu_opt2 material identical source records and table commit order; %u source/deferred rows: OFF %.4f ON %.4f ms/query\n",count,off.first,on.first);}
}
