## VRAM iteration 3 (branch codex-vram-shrink2, READY_FOR_LIVE, 2026-11-02)

- No commit or git metadata writes. No game launch. Existing keys/defaults and
  ABI unchanged; shaders/variants.txt request delta 0. Layer restart required.
- Evidence: vg2 21:38 recovery reached 15178 / 15640 MiB. Cascade shrank
  3 x 544 -> 3 x 352 MiB at 21:42:10; the final return to 1632 MiB follows
  restore disabling elastic at 21:42:52. It is legacy key-off restoration.
- Supersedes capture sizing in codex-capture-reserves/codex-vram-shrink:
  retain a rolling 24-sample peak across replacement, keep 25% demand reserve,
  require another 25% capacity step before shrinking. Grow when actual demand
  exceeds capacity, not merely when spare reserve falls below 25%. Existing
  detach/fence retention remains. Elastic growth also works with guard off.
- The 24000 MiB ask is 25 retries x 960 MiB, not one allocation. Refusal windows
  now report attempt count and peak allocation, without accumulating retries.
  RT capture has its own consumer label. Full replacement bytes still go to the
  guard: the old allocation remains alive, so using only the delta is unsafe.
- Pool deferred lines name low-demand qualification at 123 / 512 and 187 / 1018
  MiB. At final 1050 / 2047 MiB there is no target: quarter-full fails silently.
  Supersedes codex-mesh-pool policy: target max(start, peak used x 1.25), require
  at least 25% savings. Retain peak target across the 254-frame qualification.
  Native hair, pending writers/ranges and retirement safety gates stay intact.
- At 1050 MiB, CPU target is 1312.5 MiB: potential 734.5 MiB active reserve
  saving after retirement. This is a prediction, not a measured memory pass.
- Job expects actual pending-producer/detached-slot events. Memory snapshots
  use event gates with 1 s settling; plan regenerated without phase changes.
  All four capture expressions match the supplied log. Fake queue dry run and
  queue-gate Python tests/lints pass. Job remains outside pending inboxes.
- Validation: C++ layer build, 6/6 targeted CTests; elastic 481 checks, compact
  94, guard 122. First dry run failed because py was absent; temporary build-only
  launcher shim fixed the environment and the full fake queue dry run passed.
- Validation mistake: the general build invoked DXC for the unrelated
  native_hit_gpu_fixture despite the no-shader-tools constraint. No further
  shader tools run; subsequent verification uses explicit C++ targets only.
- Real GPU copy cost, process recovery, capture coverage, energy and visual
  invariance remain unmeasured. Keys stay off. Owner's later S8 low-view vg2
  should show published pool repack and retired overlap in recover-on, then
  expected legacy cascade restoration when the bundle turns off.
