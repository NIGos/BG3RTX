#include "../layer/sebridge.h"
#include <cstdio>
#include <string>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
namespace bg3 {
void log(const char*,...) {}
int cfg_int(const char*,int fallback){return fallback;}
void make_dirs(const std::string&) {}
bool write_file(const std::string&,const void*,size_t){return true;}
}
std::string export_text(const std::string& membership){return "BG3RTX_SE 1\nseq 1 level CTY_Main_A host 0 10 0 "+membership+"\nL 9 1 0 1 10 1 4 1 1 1 10 6500 1 191 0 0 0 0 Lamp\nEND\n";}
int main(){
 const float near[]={1,10,1},outside[]={100,10,1};unsigned checks=0;
 auto check=[&](const std::string& header,bool complete){bg3::SeSnapshot s;assert(bg3::se_parse_lights(export_text(header),s));assert(s.lights.size()==1&&s.lights[0].id==9&&s.lights[0].flags==1);assert(bg3::se_classification_complete(s)==complete);assert(bg3::se_covers(s,near)==complete);assert(!bg3::se_covers(s,outside));++checks;};
 check("all 3877 classified 0 pending 3877 out 1",false); // first export before classification
 check("all 3877 classified 100 pending 3777 out 1",false);
 check("all 3877 classified 3877 pending 0 out 1",true);
 check("all 3877 classified 3877 pending 1 out 1",false); // same-count replacement queued
 check("all 3800 classified 3877 pending 0 out 1",false); // legacy cumulative counter cannot prove absence
 check("all 3877 classified 3877 out 1",false); // old client, explicit records remain useful
 check("all 3877 pending 0 out 1",false);
 check("all 3877 classified 3877 pending 0 out 2",false); // malformed row was skipped / incomplete body
 check("classified 3877 pending 0 out 1",false);
 check("all 0 classified 0 pending 0 out 1",false); // inconsistent emitted count cannot prove absence
 {bg3::SeSnapshot empty;assert(bg3::se_parse_lights("BG3RTX_SE 1\nseq 1 level X host 0 10 0 all 0 classified 0 pending 0 out 0\nEND\n",empty));assert(bg3::se_covers(empty,near));++checks;}
 for(const auto& header:{"all -1 classified 0 pending 0","all 1 classified invalid pending 0","all 1 classified 1 pending 4294967296","all 1 classified 1 pending -1"}){
  bg3::SeSnapshot s;assert(!bg3::se_parse_lights(export_text(header),s));++checks;
 }
 bg3::SeSnapshot reused;assert(bg3::se_parse_lights(export_text("all 1 classified 1 pending 0"),reused));
 assert(bg3::se_parse_lights(export_text("all 1 classified 1"),reused));assert(!bg3::se_classification_complete(reused));++checks;
 std::printf("SE classification absence: %u scenarios PASS; explicit partial/legacy records retained\n",checks);
}
