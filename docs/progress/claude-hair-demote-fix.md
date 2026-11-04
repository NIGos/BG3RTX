## Strand variants return after every discard (branch claude/hair-demote-fix, READY_FOR_LIVE, 2026-11-04)

- hv8 Aftermath: the page fault (VA 0x2564a00000, far past the mesh pool) is in rt/hair_primary_gbuffer.hlsl (the
  coverage probe instance), SPIR-V line 1352: the strand points load right after OpDemoteToHelperInvocation. DXC lowers
  discard to demote; a demoted lane keeps executing, so after hp_segment failed (i0 garbage from the segment table) the
  points load still ran. Same class as the hc1/hv5/hv6/hv7 losses (all reads far past the pool, all once rays hit).
- Fix: in the strand G-buffer and velocity variants every discard returns at once; the card UV derivatives move before
  the first return. Live check: hv7 probe-only and all-passes windows without device loss.
