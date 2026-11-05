## L11/L12/L17 submission redo (branch layer-submit2, READY_FOR_LIVE, 2026-11-06)

- Confirmed in code: unowned RR descriptor ring, NGX recording-time initialization,
  and temporal history published from unsubmitted recordings.
- Causes in ae625cef: layer/ngx.cpp:1264; layer/rrpost.cpp:470,1152.
- Read reverted ae625cef. Its global pending guide gate rejects an evaluate recorded
  before the separate producer is submitted; no temporal/final is reached there.
  A later submit is too late for that evaluate. The fixed 256 leased sets can also fill.
- Primary submits publish exact secondary IDs captured at ExecuteCommands, never
  the secondary handle's newer recording. Failed Begin retains the previous ID.
- Descriptor sets stay immutable until recording discard and proven GPU completion.
  New pool pages handle retained executable recordings without dropping post passes.
- Every full-overwrite NGX guide writer carries its own ordered layout transition.
  Only successful submission publishes initialization; no global pending gate blocks RR.
- Per-slot history metadata survives recording N+1 before submit N. Only submitted
  previous-frame history is sampled; absent history still dispatches the fresh temporal
  sample and final grade. Discard cannot publish; an older resubmission invalidates metadata.
- CPU tests execute the production allocator, submit walker and temporal function:
  600 retained sets, primary/secondary IDs, overlapping N/N+1, discard/re-begin,
  97 steady-state history reads, frame gap and older writer resubmission.
- post_recording_test is registered in CMake and merge_scope.py's build-only list.
- Shader identity: 218/218 requests byte-identical to HEAD 5256bec2d25a by identical
  input keys (0 compiles); shaders/variants.txt request delta 0. ABI and presets unchanged.
- Validation: final scripts/build.ps1 -Test PASS 196/196 (build 20 s, CTest 180 s).
- No game launch, queue writes or git writes. build.ps1 and its CTests ran BelowNormal.
- Coordinator gate: rel1-smoke-ultra-s8 must have '[rrpost] gpu temporal' lines and
  output-res grade > 0 (rejected: 0 lines / 0.000 ms; qualified: 32 / 0.079 ms).
- GPU cost and live recovery remain unconfirmed; no live authorization was requested.
