// Pins the hit-material qualification policy (common/native_hit_qualify.h).
// The reference functions below are the pre-extraction layer expressions,
// copied verbatim, checked against the header on an exhaustive grid.
#include "../common/native_hit_qualify.h"
#include <cstdio>
#include <initializer_list>
#include <cstdlib>
using namespace bg3::native_hit;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static MaterialPipelineFacts accepted_static(){
 MaterialPipelineFacts f;f.stride=64;f.vt_outputs=1;f.xfb=true;f.material_state=true;f.depth_test=1;
 f.blend_mask=0;f.color_mask=0xfffu;f.triangle_list=true;f.uv_ok=true;return f;
}
static bool ref_exact(const MaterialPipelineFacts& p){ // geocapture.cpp before D2b
 const bool outputs=p.tileset ? p.tileset_capture && p.stride>=80u && p.tileset_outputs==3u && p.tileset_inputs_valid : p.vt_outputs!=0u;
 return p.xfb && outputs && p.material_state && p.depth_test==1 && p.blend_mask==0 && (p.color_mask&0xf73u)==0xf73u && p.triangle_list && p.uv_ok;
}
int main(){
 // Representative accept/decline cases.
 auto f=accepted_static();CHECK(exact_material_pipeline(f));
 f.color_mask=0xf73u;CHECK(exact_material_pipeline(f));
 f.color_mask=0xf72u;CHECK(!exact_material_pipeline(f));
 f=accepted_static();f.depth_test=-1;CHECK(!exact_material_pipeline(f)); // unknown depth test is not exact
 f=accepted_static();f.blend_mask=1;CHECK(!exact_material_pipeline(f));
 f=accepted_static();f.vt_outputs=0;CHECK(!exact_material_pipeline(f));
 f=accepted_static();f.tileset=true;CHECK(!exact_material_pipeline(f)); // tileset needs its own outputs
 f.tileset_capture=true;f.stride=80;f.tileset_outputs=3;f.tileset_inputs_valid=true;f.vt_outputs=0;CHECK(exact_material_pipeline(f));
 f.stride=79;CHECK(!exact_material_pipeline(f));
 f.stride=80;f.tileset_outputs=2;CHECK(!exact_material_pipeline(f));
 // Exhaustive grid against the original expression.
 for(unsigned bits=0;bits<(1u<<14);++bits){
  MaterialPipelineFacts g;g.tileset=bits&1;g.tileset_capture=bits&2;g.stride=(bits&4)?80:64;g.tileset_outputs=(bits&8)?3:2;
  g.tileset_inputs_valid=bits&16;g.vt_outputs=(bits&32)?1:0;g.xfb=bits&64;g.material_state=bits&128;g.depth_test=(bits&256)?1:0;
  g.blend_mask=(bits&512)?1:0;g.color_mask=(bits&1024)?0xf73u:0x073u;g.triangle_list=bits&2048;g.uv_ok=bits&4096;
  if(bits&8192)g.depth_test=-1;
  CHECK(exact_material_pipeline(g)==ref_exact(g));
 }
 // Deferred qualification: family first, then pipeline/kind/instances/stride.
 CHECK(deferred_qualify(false,false,true,0,1,64)==DeferredQualify::FamilyDeclined);
 CHECK(deferred_qualify(false,true,false,9,0,0)==DeferredQualify::FamilyDeclined);
 CHECK(deferred_qualify(true,false,true,0,1,64)==DeferredQualify::Accept);
 CHECK(deferred_qualify(true,false,true,1,1,64)==DeferredQualify::Accept);
 CHECK(deferred_qualify(true,false,true,2,1,64)==DeferredQualify::PipelineDeclined); // indirect
 CHECK(deferred_qualify(true,false,true,0,2,64)==DeferredQualify::PipelineDeclined); // instanced static
 CHECK(deferred_qualify(true,false,true,0,1,63)==DeferredQualify::PipelineDeclined);
 CHECK(deferred_qualify(true,false,false,0,1,64)==DeferredQualify::PipelineDeclined);
 CHECK(deferred_qualify(true,true,true,0,40,80)==DeferredQualify::Accept);           // tileset instancing allowed
 CHECK(deferred_qualify(true,true,true,0,40,79)==DeferredQualify::PipelineDeclined);
 for(uint32_t kind=0;kind<6;++kind)for(uint32_t inst=0;inst<3;++inst)for(uint32_t stride:{0u,63u,64u,79u,80u,96u})
  for(unsigned b=0;b<8;++b){
   bool audited=b&1,tileset=b&2,exact=b&4;
   DeferredQualify want=!audited?DeferredQualify::FamilyDeclined:
    (!exact || kind>1 || (!tileset && inst!=1) || stride<(tileset?80u:64u))?DeferredQualify::PipelineDeclined:DeferredQualify::Accept;
   CHECK(deferred_qualify(audited,tileset,exact,kind,inst,stride)==want);
  }
 // Prepass geometry and alpha record.
 CHECK(prepass_geometry_ok(false,300,1,100,64));
 CHECK(!prepass_geometry_ok(false,300,2,100,64));
 CHECK(!prepass_geometry_ok(false,303,1,100,64));
 CHECK(prepass_geometry_ok(false,300,1,100,16));   // static: stride unchecked here
 CHECK(prepass_geometry_ok(true,600,2,200,80));
 CHECK(!prepass_geometry_ok(true,600,2,200,79));
 CHECK(!prepass_geometry_ok(false,0xffffffffu,1,0x55555555u+1,64));
 CHECK(prepass_alpha_ok(true,5,7,0.f,0.f));
 CHECK(!prepass_alpha_ok(true,5,0,0.5f,0.f));
 CHECK(!prepass_alpha_ok(true,5,0,0.f,-1.f));
 CHECK(!prepass_alpha_ok(true,1,0,0.f,0.f));
 CHECK(prepass_alpha_ok(false,1,0,9.f,9.f));
 CHECK(!prepass_alpha_ok(false,1,1,0.f,0.f));
 CHECK(!prepass_alpha_ok(false,5,0,0.f,0.f));
 // Opaque prepass.
 CHECK(opaque_prepass_geometry_ok(0,1,300,100,64));
 CHECK(opaque_prepass_geometry_ok(1,1,300,100,96));
 CHECK(!opaque_prepass_geometry_ok(2,1,300,100,64));
 CHECK(!opaque_prepass_geometry_ok(0,2,300,100,64));
 CHECK(!opaque_prepass_geometry_ok(0,1,301,100,64));
 CHECK(!opaque_prepass_geometry_ok(0,1,300,100,63));
 // Instanced draws stay declined for the audited static pair (a per-draw UBO-matrix VS).
 CHECK(deferred_qualify(true,false,true,0,2,64)==DeferredQualify::PipelineDeclined);
 CHECK(!opaque_prepass_geometry_ok(0,2,300,200,64));
 // Family census: per-pair counts, instanced and STI totals, bounded entries, top by vertices.
 {FamilyCensus c;c.add(1,2,1,300);c.add(1,2,4,1200);c.add(3,kVtStiDeferredFs,8,2400);c.add(5,6,1,30);
  CHECK(c.draws==4 && c.instanced==2 && c.sti==1 && c.entries.size()==3 && c.other==0);
  auto t=c.top(2);CHECK(t.size()==2 && t[0].fs==kVtStiDeferredFs && t[0].vertices==2400 && t[1].vs==1 && t[1].draws==2 && t[1].instanced==1 && t[1].vertices==1500);
  CHECK(c.top(10).size()==3);
  FamilyCensus full;for(uint32_t i=0;i<FamilyCensus::kMaxEntries+5;++i)full.add(i,i,1,3);
  CHECK(full.entries.size()==FamilyCensus::kMaxEntries && full.other==5 && full.draws==FamilyCensus::kMaxEntries+5);
  full.add(0,0,1,3);CHECK(full.entries[0].draws==2 && full.other==5);
  FamilyCensus tie;tie.add(1,1,1,6);tie.add(2,2,1,3);tie.add(2,2,1,3);auto u=tie.top(1);CHECK(u[0].vs==2);}
 std::printf("native_hit_qualify_test: %u checks passed\n",checks);
 return 0;
}
