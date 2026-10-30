#include "../common/omm_submit_state.h"
#include <cstdio>
#include <cstdlib>
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
  std::printf("OMM submit graph/generation/queue/counter checks %u; no device calls\n",checks);
}
