#include "../common/sl_present_metering.h"
int main() {
  if (sl_enable_present_metering(false,true,true)) return 1;
  if (sl_enable_present_metering(true,false,true)) return 2;
  if (sl_enable_present_metering(true,true,false)) return 3;
  if (!sl_enable_present_metering(true,true,true)) return 4;
  // Caller-owned false and true fields both survive the temporary override;
  // repeated restoration cannot write a pointer after vkCreateDevice returns.
  for (uint32_t initial=0;initial<=1;++initial) {
    uint32_t caller=initial;
    SlMeteringOverride saved; saved.capture(caller); caller=1; saved.restore();
    if (caller!=initial || saved.caller) return 5;
    caller=99; saved.restore(); if (caller!=99) return 6;
  }
  SlMeteringOverride untouched; untouched.restore();
  return 0;
}
