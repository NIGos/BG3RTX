#pragma once
#include <cstdint>
namespace bg3::omm {
// A per-dispatch scope, not a per-frame cache. Each actual traversal command
// must register its own ticket references, even if a prior command succeeded.
enum class BindingState {Ordinary,Accelerated,Reference,Unavailable};
template<class Handle> class SceneBinding {
  Handle& target_;Handle original_;BindingState state_=BindingState::Ordinary;
 public:
  template<class Validate> SceneBinding(Handle& target,bool required,Validate&& validate,uint64_t expected_identity=0):target_(target),original_(target){
    if(!required)return;
    Handle reference{};uint64_t identity=expected_identity;
    const bool fast=validate(original_,reference,identity);
    if((expected_identity && identity!=expected_identity) || reference==Handle{} || (fast && (!identity || original_==Handle{}))){state_=BindingState::Unavailable;return;}
    if(fast)state_=BindingState::Accelerated;
    else{target_=reference;state_=BindingState::Reference;}
  }
  SceneBinding(const SceneBinding&)=delete;SceneBinding& operator=(const SceneBinding&)=delete;
  ~SceneBinding(){target_=original_;}
  bool valid()const{return state_!=BindingState::Unavailable;}
  BindingState state()const{return state_;}
};
}
