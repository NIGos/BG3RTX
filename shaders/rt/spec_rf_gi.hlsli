#pragma once
#ifdef RF_SPEC_HIT_GI
#ifdef RF_HIT_GI
#error RF_SPEC_HIT_GI replaces RF_HIT_GI
#endif
[[vk::constant_id(1013)]] const uint kSpecRfHitGi = 0;
#define RF_HIT_GI 1
#define RF_GI_ON (kSpecRfHitGi != 0u)
#else
#define RF_GI_ON (RF_HIT_GI != 0)
#endif
