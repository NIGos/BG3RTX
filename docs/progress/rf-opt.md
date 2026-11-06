## Default Ultra RF mismatch (branch rf-opt, READY_FOR_RECAPTURE, 2026-11-06)

- Inn pack frame 1981: specular differs in 1,270,225 bytes / 420,526 pixels;
  diffuse, R16 hit distance and every raw counter match; guards 0.
- (a) RF uses the LS two-phase path: raw pre-dispatch `img6.bin` restores
  gSpecular before every replay; post-dispatch reference is `s0b7.live`.
  They have separate files and disjoint staging ranges, both format 122.
- (b) core/gi.cpp brackets exactly one RF dispatch with phase 1/2 callbacks
  in one command buffer. LS precedes it; GI composite/rrpost/water follow it.
  No other rendering pass writes between these snapshots.
- (c) All 2,234,064 depth-valid no-hit pixels round-trip initial/live/replay
  exactly. Hit differences reach hundreds of native channel codes, not a
  one-code packing rounding error. Captured view formats are recreated raw.
- Missing sampler bias/minLod and other ordinary state is a confirmed capture
  gap, not the proven cause. STATE1 now preserves it; unsupported extensions
  fail capture. The old pack cannot recover lost fields. Host-only UBOs still
  use record-time mapped bytes; the new contract reports their count.
- Added tested two-phase completeness checks and a fixed-writer marker for
  all three raw output pairs and 20/20 STATE1 sampler descriptors.
- Prepared tools/live_batches/rf_pack_state1 (copied rf_pack): serial 61
  stats OFF, serial 62 rt_stats/rf_stats ON; 65 s static plan, about 1 GB.
  Both new packs must pass `--require-live-equal` before optimisation resumes.
- Baseline module equals runtime cache SHA256 27ea62e4...; repeats exact.
  Baseline registers 128, shared bytes 0; replay ~0.79 ms is not live ~2.2 ms.
- No rf_fast/1041 allocation or gain claim. Cost ablations prepared, not run.
  Shader requests unchanged; 218/218 byte-identical to HEAD, ABI unchanged.
- BelowNormal build succeeded: full CTest 199/200; missing frozen regex entries
  fixed additively, then batch lint/live_flow/merge_scope recheck PASS 3/3.
- Evidence: .scratch/rf-opt/{validation,strict-validation,packed-analysis}.log.
- Offline only: no game launch, queue staging or Git writes. Stop for capture.
