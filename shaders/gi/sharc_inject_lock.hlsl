// dxc: -enable-16bit-types
// sharc_inject.hlsl for a device without shaderBufferInt64Atomics: hash keys are inserted
// through SHaRC's lock buffer (set 4 binding 5) instead of 64-bit compare-exchange.
#define HASH_GRID_ENABLE_64_BIT_ATOMICS 0
#include "sharc_inject.hlsl"
