// GPU-execution epochs. No push constants; ordinary native push values survive.
[[vk::binding(0,0)]] RWByteAddressBuffer globalEpoch;
[[vk::binding(1,0)]] RWByteAddressBuffer imageEpoch;
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
[numthreads(1,1,1)]
void main(){increment(false);increment(true);}
