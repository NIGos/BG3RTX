# GI replay packs (2026-11-08): not needed, run failed

- Run 1 (Lower City, user go 2026-11-08): INVALID. The GI pack recorded (frame 2078, 1.28 GB) but the job's
  `ls_k=0` with `ls_box=2` made the LS writer log "pack 1 failed: LS capture requires production non-OMM
  box-k pipeline", which matched the forbid pattern; fail-fast closed the game mid-write (partial pack deleted).
  Fix if ever rerun: forbid only `pack 1 failed: gi_hit_direct` / `over budget`, or set `ls_box=0`.
- Not rerun: complete GI packs already exist in the test tree (camp 013545-2, inn 013914-3, S8 142819-1),
  so the SER and GI cost analysis runs offline on those.
