#include "../layer/terrain_packet_contract.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <limits>
using namespace bg3::terrain_packet;
int main(){
 assert(buffer_prefix(1024,256,80,80,80,80)==80);
 assert(buffer_prefix(16384,1024,UINT64_MAX,448,7168,112)==7168);
 assert(buffer_prefix(1024,256,449,448,7168,112)==448);
 assert(buffer_prefix(1024,256,447,448,7168,112)==0);
 assert(buffer_prefix(1024,256,900,80,80,80)==0);
 assert(buffer_prefix(1024,255,80,80,80,80)==0);
 assert(buffer_prefix(1024,1024,80,80,80,80)==0);
 assert(buffer_prefix(UINT64_MAX,UINT64_MAX-3,8,80,80,80)==0);
 assert(buffer_prefix(1024,0,80,80,80,0)==0);
 assert(!overlap(0,80,80,80));assert(overlap(0,80,79,80));
 assert(overlap(UINT64_MAX-3,8,UINT64_MAX-1,1));assert(!overlap(1,0,1,8));
 std::puts("terrain packet range/alias: 13 checks PASS");
}
