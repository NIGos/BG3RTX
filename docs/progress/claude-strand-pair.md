## Strand resolve paired with its own frame's snapshot by submission order (branch claude/strand-pair, offline, READY_FOR_LIVE, 2026-11-09)

- Symptom: rv1-s8 (Water Queens House, fg=3, Reflex, rr_hair_strand_aa=2): every 600-frame period 600 snapshots, 0
  refused, yet 113-140 resolves fell back; Dialogue1 (fg=2) 0.
- Cause (code read): the G-buffer end (snapshot) and the post chain (resolve) are recorded on different game threads in
  different command buffers. With snap N + 1 recorded before resolve N, the recording-order Ring gave resolve N the image
  of frame N + 1 (on the GPU it still held N - 1: a wrong frame, silently) and resolve N + 1 fell back. No per-frame
  identity is visible at both sites (frame_index, the ngx G-buffer camera and the hair tag are present- or latest-based).
- Fix: common/strand_snapshot.h Pairing replaces Ring. One snapshot image (at the resolve's GPU time it holds the
  snapshot submitted last). The k-th snapshot and the k-th resolve share an ordinal; layer/rrpost.cpp
  rrpost_submit_begin (layer.cpp, before vkQueueSubmit and vkQueueSubmit2) writes the resolve's host-visible flag with
  its token only when the snapshot submitted last carries its ordinal and was written. shaders/rr_strand_aa.hlsl
  strand_resolve otherwise reads no snapshot (exactly the game's resolve). A mismatch resyncs the offset. Refused
  snapshots (no camera) and resolves after a broken chain still count, so both sides follow the same frames.
- Layer + one shader request (rr_strand_aa.hlsl strand_resolve; no other request changes); game restart; no ABI change.
  Snapshot memory 2 -> 1 RGBA16F image (-22 MB at 2560x1067). CPU: a few compares per submit.
- Test: hair_groom_asset_test test_strand_snapshot_pairing (in order, overlapped snap N/N+1 then resolve N/N+1,
  inverted, 600 mixed frames: all own frame, 0 refused; refused or missing snapshot, missing resolve, out-of-order
  batches, resubmission: refused, never a wrong frame). build -Test 220/220; rr_strand_aa.hlsl compiles and passes
  spirv-val (shader_identity --validate). host_atomics_lint classifies the flags (PairFlags, read only).
- Live check (same scene as rv1-s8, fg=3): the period line '[rrpost] strand resolve ... N frames, M fell back ... (no
  snapshot of the frame: a at recording, b at submit); pairing: r resyncs, snapshots recorded up to k ahead' should read
  M near 0 with N + M about 600, r near 0 in steady play, and k >= 1 (confirms the overlap). If b stays high with r
  high, the batches are not in frame order (the premise): then it fails safe (the game's resolve).
