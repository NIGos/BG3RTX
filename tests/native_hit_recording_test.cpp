#include "../common/native_hit_recording.h"
#include "../common/native_hit_join.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <thread>
#include <vector>
using namespace bg3::native_hit;
struct Candidates {DrawJoin join;std::vector<uint32_t> rows;};
int main(){
 Candidates value;value.join.begin(10,100);
 auto first=std::make_shared<RecordingList<Candidates>>(10,100,std::move(value));
 auto second=std::make_shared<RecordingList<Candidates>>(20,200,Candidates{});
 assert(first->current(10,100));assert(!first->current(10,101));
 assert(!first->current(11,100));assert(!first->current(0,0));
 // Holding the first recording's mutex does not block an independent writer.
 std::unique_lock held(first->mutex);
 std::thread writer([&]{std::lock_guard lock(second->mutex);second->value.rows.push_back(42);});
 writer.join();assert(second->value.rows.size()==1);held.unlock();
 DrawKey key;key.words[0]=7;
 {std::lock_guard lock(first->mutex);
  assert(first->value.join.note(10,100,key,3));
  assert(!first->value.join.note(10,100,key,4));
  first->value.rows={3,4};}
 auto snapshot=first->seal();assert(first->seal()==snapshot);
 uint32_t record=0;assert(!snapshot->join.unique_source(key,record));
 assert(snapshot->rows==std::vector<uint32_t>({3,4}));
 // A new recording and releasing registry/command ownership cannot mutate or
 // reclaim the immutable snapshot still held by a scene-preparation reader.
 first=std::make_shared<RecordingList<Candidates>>(10,101,Candidates{});
 assert(first->current(10,101));assert(snapshot->rows.size()==2);
 first.reset();assert(snapshot->rows[0]==3);assert(snapshot->rows[1]==4);
}
