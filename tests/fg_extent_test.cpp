// common/fg_extent.h: FG inputs when the swapchain is larger than the game's output. No Vulkan.
#include "../common/fg_extent.h"
#include <cstdio>
#include <cstdlib>
using namespace bg3::fg;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static bool eq(Extent a, Extent b){return a.w==b.w && a.h==b.h;}
static bool eq(Rect a, Rect b){return a.x==b.x && a.y==b.y && a.w==b.w && a.h==b.h;}
int main(){
 const Extent sc{3840,2160},game{3840,1600},bloom{1920,800},lut{32,32};
 // Same size: the pass extent, whatever the key (the old behaviour).
 const Extent same[]={{3840,1600},{1920,800}};
 CHECK(eq(output_extent(same,2,game,false),game) && eq(output_extent(same,2,game,true),game));
 // Fake fullscreen 3840x2160 desktop, 3840x1600 game: found only with the key.
 const Extent box[]={bloom,game,lut};
 CHECK(eq(output_extent(box,3,sc,false),Extent{}));
 CHECK(eq(output_extent(box,3,sc,true),game));
 // Scaled (2560x1080 game on the 2160 desktop) and pillarbox (2880x2160).
 const Extent scaled[]={{2560,1080},{1280,540}};CHECK(eq(output_extent(scaled,2,sc,true),Extent{2560,1080}));
 const Extent pillar[]={{2880,2160}};CHECK(eq(output_extent(pillar,1,sc,true),Extent{2880,2160}));
 // Half resolution or smaller (a quarter of the area) and larger than the pass: never the output.
 const Extent small[]={{1920,1080},lut};CHECK(eq(output_extent(small,2,sc,true),Extent{}));
 const Extent big[]={{5120,2160}};CHECK(eq(output_extent(big,1,sc,true),Extent{}));
 // Backbuffer subrect: whole image when equal; viewport when inside; else centred.
 CHECK(eq(backbuffer_rect(game,game,true,0,0,3840,1600),Rect{0,0,3840,1600}));
 CHECK(eq(backbuffer_rect(game,sc,true,0,280,3840,1600),Rect{0,280,3840,1600}));
 CHECK(eq(backbuffer_rect(game,sc,false,0,0,0,0),Rect{0,280,3840,1600}));
 CHECK(eq(backbuffer_rect(Extent{2560,1080},sc,true,0,0,3840,1620),Rect{0,0,3840,1620})); // scaled up
 CHECK(eq(backbuffer_rect(game,sc,true,0,1000,3840,1600),Rect{0,280,3840,1600}));          // viewport outside: centred
 CHECK(eq(backbuffer_rect(game,sc,true,0,0,3840,2160),Rect{0,0,3840,2160}));               // full-screen viewport
 std::printf("fg_extent_test: %u checks passed\n",checks);
 return 0;
}
