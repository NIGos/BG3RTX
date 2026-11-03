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
 assert(borrow_query(20,21,2,2,7,7));
 assert(!borrow_query(20,20,2,2,7,7));
 assert(!borrow_query(20,22,2,2,7,7));
 assert(!borrow_query(20,19,2,2,7,7));
 assert(!borrow_query(UINT64_MAX,0,2,2,7,7));
 assert(!borrow_query(20,21,1,2,7,7));
 assert(!borrow_query(20,21,2,2,6,7));
 QueryOwners users;assert(!users.retired([](auto){return true;}));
 users.add(100);users.add(200);users.add(200);assert(users.ids.size()==2);
 // Producer retired, borrowing recording still records or executes: keep query GPU objects.
 assert(!users.retired([](auto id){return id==100;}));
 assert(!users.retired([](auto id){return id==200;}));
 assert(users.retired([](auto){return true;}));
 RecordingOwner producer{10,100},borrower{20,200};
 assert(producer.reserve());producer.finish(true,20);
 auto done=[&](uint64_t id){return id==100?producer.retired(100,22):borrower.retired(200,22);};
 assert(!users.retired(done)); // borrower has not submitted or reset
 assert(borrower.reserve());borrower.finish(true,21);
 assert(!users.retired(done)); // producer GPU-complete, borrower in flight
 assert(users.retired([&](uint64_t id){return id==100?producer.retired(100,23):borrower.retired(200,23);}));
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
