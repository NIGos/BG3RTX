#pragma once
#ifdef WATER_HIT_COST_REFERENCE
static const uint kWaterHitCost = WATER_HIT_COST_REFERENCE;
#else
[[vk::constant_id(1021)]] const uint kWaterHitCost = 0u;
#endif
