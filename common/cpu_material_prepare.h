#pragma once
#include <mutex>
#include <utility>
namespace bg3::cpu_opt {
enum class MaterialPrepare { Ready, CaptureFailed, StageFailed };
// Capture and staging touch draw-local values and tracker-owned facts only.
// Return with the provider lock owned even on rejection or an exception, so
// callers can account failures/publish into their packet under its mutex.
template<class Mutex,class Capture,class Stage>
MaterialPrepare prepare_material(std::unique_lock<Mutex>& lock,bool unlock_capture,bool stage_outside,
                                 Capture&& capture,Stage&& stage){
 const bool unlocked=unlock_capture || stage_outside;
 if(unlocked)lock.unlock();
 bool captured=false,staged=true;
 try {
  captured=std::forward<Capture>(capture)();
  if(captured && stage_outside)staged=std::forward<Stage>(stage)();
 }catch(...){if(unlocked)lock.lock();throw;}
 if(unlocked)lock.lock();
 if(!captured)return MaterialPrepare::CaptureFailed;
 if(!stage_outside)staged=std::forward<Stage>(stage)();
 return staged?MaterialPrepare::Ready:MaterialPrepare::StageFailed;
}
} // namespace bg3::cpu_opt
