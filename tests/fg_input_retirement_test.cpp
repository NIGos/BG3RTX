#include "../common/fg_input_retirement.h"
#include <unordered_map>
int main() {
  FgInputUse use;
  if (fg_retire_policy(true,nullptr,false)!=FgRetirePolicy::Defer) return 1;
  if (fg_retire_policy(true,nullptr,true)!=FgRetirePolicy::Global) return 2;
  use.tag(10);
  if (fg_retire_policy(true,&use,true)!=FgRetirePolicy::Defer) return 3;
  use.publish(9,1,99); use.publish(10,0,100); use.publish(10,1,0);
  if (!use.pending) return 4;
  use.publish(11,1,101); // Later present safely covers failed frame-10 query.
  if (use.pending || use.value!=101 || fg_retire_policy(true,&use,true)!=FgRetirePolicy::Exact) return 5;
  const auto old=use;
  use.publish(20,1,200); // Unrelated newer global frame must not extend this use.
  if (!use.same(old)) return 6;
  if (fg_retire_policy(false,&use,false)!=FgRetirePolicy::Global) return 7;
  // Two tagged views share an image's latest use. Re-tagging any view invalidates
  // a completion snapshot and cannot retire using its previous fence/value.
  std::unordered_map<uint64_t,FgInputUse> images;
  std::unordered_map<uint64_t,uint64_t> views{{100,42},{101,42}};
  images[42]=old;
  images[views[101]].tag(30);
  if (!images[views[100]].pending || images[42].same(old)) return 8;
  images[42].publish(30,2,1); // Different semaphore: never compare numeric values.
  if (!images[42].known() || images[42].fence!=2 || images[42].value!=1) return 9;
  images.erase(42); views.clear(); // Actual destruction/device reset; handle reuse.
  images[42].tag(40);
  if (images[42].known() || !images[42].pending) return 10;
  images[42].boundary(false); // Failed idle/swapchain boundary cannot prove completion.
  if (fg_retire_policy(true,&images[42],true)!=FgRetirePolicy::Defer) return 11;
  images[42].publish(50,3,1000);
  if (images[42].known() || !images[42].invalidated) return 14;
  images[42].tag(51); images[42].publish(51,3,1001);
  if (images[42].known() || fg_retire_policy(false,&images[42],true)!=FgRetirePolicy::Defer) return 15;
  images[42].boundary(true);
  if (fg_retire_policy(true,&images[42],false)!=FgRetirePolicy::Complete) return 12;
  images[42].boundary(false); // Preserve a real idle proof across SDK ownership expiry.
  if (fg_retire_policy(true,&images[42],false)!=FgRetirePolicy::Complete) return 16;
  images[42].tag(41);
  if (images[42].idle || !images[42].pending) return 13;
  return 0;
}
