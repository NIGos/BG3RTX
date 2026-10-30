#include "../common/omm_submit_state.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include "../common/omm_backend_plan.h"
using namespace bg3::omm;
static unsigned checks;
static void check(bool v){++checks;if(!v){std::fprintf(stderr,"check %u failed\n",checks);std::exit(1);}}
int main(){
  CommandGraph g;std::set<Id> out;
  check(!g.add(1,100,3)); // Unknown CB cannot register OMM.
  g.allocate(1,10,3);g.allocate(2,10,3);g.allocate(3,11,4);
  check(!g.add(1,100,3));check(g.begin(1));check(!g.add(1,0,3));check(g.add(1,100,3));
  check(g.begin(2));check(g.add(2,200,3));check(g.end(2,true));
  check(g.execute(1,2));check(g.end(1,true));
  const Id primary[]={1};check(g.submitted(primary,out));check(out==std::set<Id>({100,200}));
  check(g.referenced(100));check(g.referenced(200));
  // Replaying exactly the same executable recording retains both references.
  check(g.submitted(primary,out));check(out.size()==2);
  auto dropped=g.reset(2);check(dropped.empty()); // Parent still references old generation.
  check(g.begin(2));check(g.add(2,300,3));check(g.end(2,true));
  check(!g.submitted(primary,out));check(out==std::set<Id>({100,200})); // No substitution of new child.
  dropped=g.reset(1);check(dropped==std::set<Id>({100,200}));
  check(!g.referenced(100));check(!g.referenced(200));check(g.referenced(300));
  check(g.begin(1));check(g.execute(1,2));check(g.end(1,true));
  check(g.submitted(primary,out));check(out==std::set<Id>({300}));
  check(g.begin(3));check(!g.add(3,400,3));check(g.add(3,400,4));check(g.end(3,true));
  check(queue_allowed(true,true));check(queue_allowed(false,false));check(!queue_allowed(false,true));
  const Id unrelated[]={999};check(g.submitted(unrelated,out));check(out.empty());
  g.reset(2,true);check(!g.submitted(primary,out));check(out==std::set<Id>({300}));
  g.allocate(2,10,3);check(g.begin(2));check(g.add(2,500,3));check(g.end(2,true));
  check(!g.submitted(primary,out));check(out==std::set<Id>({300})); // Reused handle is a new generation.
  check(g.reset(1)==std::set<Id>({300}));check(g.reset(2,true)==std::set<Id>({500}));
  check(!g.referenced(300));check(!g.referenced(500));
  g.allocate(20,10,3);g.allocate(21,10,3);check(g.begin(21));check(g.mark_content(21));check(g.end(21,true));check(g.begin(20));check(g.execute(20,21));check(g.end(20,true));check(g.has_content(20));
  g.reset(21);check(g.has_content(20));g.reset(20);check(!g.has_content(20));check(!g.mark_content(999));
  // Failed application submits never call signal; no completion serial exists.
  SubmitSerial s;check(s.candidate()==1);check(s.lastSignal==0);check(s.completed(0,true));
  check(s.signal(1,true));check(s.candidate()==2);check(s.completed(0,true));
  check(s.signal(2,true));check(s.completed(1,true));check(s.completed(2,true));
  check(!s.completed(1,true));check(s.poisoned);check(!s.candidate());
  s={};check(!s.signal(1,false));check(s.poisoned);check(s.lastSignal==0);check(!s.completed(1,true));
  s={};check(!s.signal(2,true));check(s.poisoned);
  s={};check(s.signal(1,true));check(!s.completed(2,true));check(s.poisoned);
  s={};check(s.signal(1,true));check(!s.completed(0,false));check(s.poisoned);
  s={};s.lastSignal=UINT64_MAX;check(!s.candidate());
  // Dummy input ownership traverses the actual lifetime/recording gates: reset
  // cannot release a submitted owner; timeline completion alone cannot release
  // an executable recording that can replay its ticket.
  for(bool submitted:{false,true}) {
    bool inputsReleased=false,objectsDestroyed=false;
    auto source=std::shared_ptr<void>(new int(1),[&](void* p){check(objectsDestroyed);inputsReleased=true;delete static_cast<int*>(p);});
    std::weak_ptr<void> weak=source;
    auto retained=source;source.reset();check(!weak.expired());
    Lifetime life;CommandGraph refs;refs.allocate(1,1,0);refs.begin(1);refs.add(1,1,0);refs.end(1,true);
    check(life.reserve());check(!life.discardable());check(!inputsReleased);
    if(submitted){check(life.use(7));check(life.retire());}
    check(life.unreserve());
    if(submitted){check(life.releasable(7));check(refs.referenced(1));check(!weak.expired());}
    refs.reset(1);
    check(!refs.referenced(1));check(submitted?life.releasable(7):life.discardable());
    objectsDestroyed=true;retained.reset();check(weak.expired());check(inputsReleased);
  }
  // Cache owner release does not invalidate already recorded executable CBs.
  for(bool submitted:{false,true}) {
    Lifetime life;CommandGraph refs;refs.allocate(1,1,0);refs.begin(1);refs.add(1,7,0);refs.end(1,true);
    if(submitted)check(life.use(10));life.release_owner();check(!life.accepts_new_use());
    check(refs.referenced(7));check(!(life.releasable(UINT64_MAX) && !refs.referenced(7)));
    const Id cmds[]={1};check(refs.submitted(cmds,out));check(out==std::set<Id>({7}));
    check(life.reserve());check(!life.releasable(UINT64_MAX));
    // Reset after preparing submission cannot drop the reservation's lifetime.
    check(refs.reset(1)==std::set<Id>({7}));check(!life.releasable(UINT64_MAX));
    check(life.use(11));check(life.unreserve());check(!life.releasable(10));check(life.releasable(11));
  }
  Lifetime replay;CommandGraph refs;refs.allocate(1,1,0);refs.begin(1);refs.add(1,7,0);refs.end(1,true);
  check(replay.use(20));replay.release_owner();check(replay.use(21));
  check(!replay.releasable(20));check(replay.releasable(21));check(refs.referenced(7));
  auto old=refs.reset(1);check(old==std::set<Id>({7}));refs.begin(1);refs.end(1,true);
  const Id cmds[]={1};check(refs.submitted(cmds,out));check(out.empty());check(!refs.referenced(7));
  std::printf("OMM submit graph/generation/queue/counter checks %u; no device calls\n",checks);
}
