// GPU-execution epochs. No push constants; ordinary native push values survive.
[[vk::binding(0,0)]] globallycoherent RWByteAddressBuffer globalEpoch;
[[vk::binding(1,0)]] globallycoherent RWByteAddressBuffer imageEpoch;
[[vk::constant_id(0)]] const uint beginWrite=0u;
void disable() {uint unused;globalEpoch.InterlockedOr(4u,1u,unused);}
void increment(bool image) {
  // Atomic read, including the global word shared by unrelated upload queues.
  // A plain Load racing another queue's CAS would not be a valid epoch proof.
  uint old;
  if(image)imageEpoch.InterlockedOr(0u,0u,old);
  else globalEpoch.InterlockedOr(0u,0u,old);
  for(;;) {
    if(old==0xffffffffu){disable();return;}
    uint observed;
    if(image)imageEpoch.InterlockedCompareExchange(0u,old,old+1u,observed);
    else globalEpoch.InterlockedCompareExchange(0u,old,old+1u,observed);
    if(observed==old){if(old==0xfffffffeu)disable();return;}
    old=observed;
  }
}
void active_write(bool begin) {
  uint old;globalEpoch.InterlockedOr(8u,0u,old);
  for(;;) {
    // No wrap or guessed pairing. Any lost begin/end permanently disables OMM.
    if((begin && old>=0xfffffffeu)||(!begin && old==0u)){disable();return;}
    uint observed;
    globalEpoch.InterlockedCompareExchange(8u,old,begin?old+1u:old-1u,observed);
    if(observed==old)return;
    old=observed;
  }
}
[numthreads(1,1,1)]
void main(){
  if(beginWrite!=0u)active_write(true);
  else {
    uint writers;globalEpoch.InterlockedOr(8u,0u,writers);
    if(writers==0u){disable();return;}
    increment(true);increment(false);
    // Interlocked updates to different words are not themselves a fence.
    DeviceMemoryBarrier();
    active_write(false);
  }
}
