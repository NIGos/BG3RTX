#pragma once
// Eligibility only. The caller evaluates the existing finite roof segment test
// after screen/cache/emission and keeps the original fallback accumulation.
bool gi_roof_reject_eligible(uint key, uint far_key, uint stats, bool visible,
                             bool aborted, float t, float base_radius, uint fallback) {
  return key != 0u && far_key != 0u && stats == 0u && !visible && !aborted &&
         t > base_radius && fallback == 0u;
}
