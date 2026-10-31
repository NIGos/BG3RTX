// Pins the native Sun receiver packet table (common/native_sun_packets.h).
#include "../common/native_sun_packets.h"
#include <cstdio>
#include <initializer_list>
#include <cstdlib>
using namespace bg3::sun_packets;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static bool eq(UboPacket p,uint32_t s,uint32_t b,uint32_t o,uint32_t n){return p.set==s&&p.binding==b&&p.offset==o&&p.size==n;}
int main(){
 CHECK(kReceiverFs==0x0687fa327aed7f8eull);
 CHECK(eq(kFrame,0,0,0,32));
 CHECK(eq(kCamera,0,2,0,224));
 CHECK(eq(kSun,2,26,0,544));
 CHECK(eq(kAuxiliary,1,0,0,144));
 CHECK(eq(kRayBasis,0,1,112,48));
 CHECK(kCsm.set==2&&kCsm.binding==8);
 CHECK(kAuxiliaryNear.set==1&&kAuxiliaryNear.binding==3);
 CHECK(kAuxiliaryFar.set==1&&kAuxiliaryFar.binding==4);
 CHECK(kEpochBytes==32&&kEpochBytes<=kFrame.size);
 const UboPacket all[]={kFrame,kCamera,kSun,kAuxiliary,kRayBasis};
 for(UboPacket p:all) CHECK(p.size<=kMaxPacketBytes);
 std::printf("native_sun_packets_test: %u checks passed\n",checks);
 return 0;
}
