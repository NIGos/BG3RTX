#include "../layer/terrain_packet_contract.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cstdlib>
#include <cstdio>
#include <limits>
using namespace bg3::terrain_packet;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
 CHECK(buffer_prefix(1024,256,80,80,80,80)==80);
 CHECK(buffer_prefix(16384,1024,UINT64_MAX,448,7168,112)==7168);
 CHECK(buffer_prefix(1024,256,449,448,7168,112)==448);
 CHECK(buffer_prefix(1024,256,447,448,7168,112)==0);
 CHECK(buffer_prefix(1024,256,900,80,80,80)==0);
 CHECK(buffer_prefix(1024,255,80,80,80,80)==0);
 CHECK(buffer_prefix(1024,1024,80,80,80,80)==0);
 CHECK(buffer_prefix(UINT64_MAX,UINT64_MAX-3,8,80,80,80)==0);
 CHECK(buffer_prefix(1024,0,80,80,80,0)==0);
 CHECK(!overlap(0,80,80,80));CHECK(overlap(0,80,79,80));
 CHECK(overlap(UINT64_MAX-3,8,UINT64_MAX-1,1));CHECK(!overlap(1,0,1,8));
 // The ACTUAL packet state wrapper uses native_hit::RecordingOwner, not a
 // parallel fake lifetime. No frame age can release an executable packet.
 Lifetime retry;retry.owner.command=71;retry.owner.recording=9;
 CHECK(!retry.finishable() && !retry.release_unsubmitted() && !retry.pending());
 CHECK(!retry.reserve(8));CHECK(retry.reserve(9));CHECK(retry.pending() && !retry.finishable());
 retry.finish(false,40);CHECK(!retry.owner.submitted && !retry.pending() && !retry.release_unsubmitted());
 CHECK(retry.reserve(9));retry.finish(true,41);
 CHECK(retry.finishable() && retry.owner.submitted_frame==41 && !retry.pending());
 // Reset while driver return is outstanding cannot release objects. Failure
 // makes a discarded recording releasable; success still requires real idle.
 Lifetime resetFail;resetFail.owner.recording=10;
 CHECK(resetFail.reserve(10));resetFail.discard();CHECK(!resetFail.release_unsubmitted() && resetFail.pending());
 CHECK(!resetFail.reserve(10));resetFail.finish(false,42);CHECK(resetFail.release_unsubmitted() && !resetFail.finishable());
 Lifetime resetSuccess;resetSuccess.owner.recording=11;
 CHECK(resetSuccess.reserve(11));resetSuccess.discard();CHECK(!resetSuccess.release_unsubmitted());
 resetSuccess.finish(true,43);CHECK(resetSuccess.finishable() && !resetSuccess.release_unsubmitted());
 Lifetime discarded;discarded.owner.recording=12;discarded.discard();
 CHECK(discarded.release_unsubmitted());CHECK(!discarded.reserve(12));
 // Multiple pins are counted even if an invalid caller attempts overlap.
 Lifetime pins;pins.owner.recording=13;
 CHECK(pins.reserve(13));CHECK(pins.reserve(13));pins.discard();pins.finish(false,44);
 CHECK(pins.pending() && !pins.release_unsubmitted() && !pins.finishable());
 pins.finish(true,45);CHECK(!pins.pending() && pins.finishable());
 // Unexpected callbacks/replay/overflow cannot manufacture retirement proof.
 Lifetime noReservation;noReservation.owner.recording=14;noReservation.finish(true,46);noReservation.discard();
 CHECK(noReservation.owner.poisoned && !noReservation.finishable() && !noReservation.release_unsubmitted());
 Lifetime overflow;overflow.owner.recording=15;overflow.owner.reservations=UINT32_MAX;
 CHECK(!overflow.reserve(15));CHECK(overflow.owner.poisoned && !overflow.finishable());
 Lifetime replay;replay.owner.recording=16;CHECK(replay.reserve(16));replay.finish(true,47);
 CHECK(replay.reserve(16));replay.finish(true,48);CHECK(replay.owner.poisoned && !replay.finishable());
 Lifetime executable;executable.owner.recording=17;executable.owner.submitted_frame=1000000;
 CHECK(!executable.finishable() && !executable.release_unsubmitted());
 CHECK(!executable.reserve(18) && !executable.pending());
 std::printf("terrain packet range/alias/submit lifetime: %u checks PASS\n",checks);
}
