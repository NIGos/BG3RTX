# Stage 4 qualification draft (2026-11-03)

Branch qual-stage4; Git metadata read-only. Source HEAD 0823a76a; main advances independently.
Exact main/tips/logs: tools/live_batches/qual_stage4/merge_audit.json. All six requested keys
are present and 0 on audited main. CPU/hm-recipe-rows/variant-migration logs are empty.
Old gi-reproj-guards has 8 unmerged commits; its implementation is superseded by main's
SpecIds 1001/1002, so do not merge its define-based path. No merge is needed for these keys.

| Item | Key(s) | Merged? | Existing gate | Predicted ON minus OFF | Job windows |
|---|---|---|---|---|---|
| Hit-material CPU | perf_hitmat_lists | Yes, off | >=600 steady frames/state; discard first CPU line; coverage/current TLAS, energy/GPU within OFF noise | waits -0.5..-0.2 ms/frame; query -0.4..-0.2, overlapping | S8 cost-hitmat-off-a/on-a/off-b/on-b |
| Hook/matrix CPU | perf_hook_state | Yes, off | >=900 frames, >=2 full hook samples, >=300 matrix submits/state; validity unchanged | hooks -0.3..-0.1 ms/frame; matrices -0.3..-0.1 ms/build | S8 cost-hooks-off-a/on-a/off-b/on-b |
| Descriptor/submit CPU | perf_submit_batch | Yes, off | >=600 frames AND 90 s/state; normalized watch CPU, no ownership loss; eligible OMM tickets and fallback coverage | CPU unknown (zero credible); clone driver calls 2->1; GPU neutral | S8 cost-submit-off-a/on-a/off-b/on-b |
| GI MV sample | gi_temporal_mv_sample | Yes, off; spec 1001 | float static energy within OFF spread; edge/inner transient <= OFF max; cost neutral | energy 0; fewer depth-edge errors | camp guard-shot-mv, guard-q/e-mv; off-a/off-b controls |
| GI previous radiance | gi_prev_radiance_guard | Yes, off; spec 1002 | edge transient < min(OFF)-spread; inner/energy within OFF spread | reduced edge transient, energy 0 | camp guard-shot-prev, guard-q/e-prev; off-a/off-b controls |
| Generated rows rerun | rt_hit_material_generated | Yes, off | mapped >0 ON / 0 OFF; clip rejected <0.04%; zero overflow/drops/timeouts; share gain > OFF spread; float energy neutral | camp +2.5 share points [1.5,4], GI +0.6 ms [0.3,1]; inn +0.3 [0,1], +0.05 ms [0,0.3] | camp + inn diag-off-a/on/off-b; cost-off-a/on/off-b; shot-off-a/on/off-b |

**Unresolved constraints:** CPU 4 x 90 s exceeds 5 min before warmup; original GI K16
transient gate needs ~11 GiB. The prepared 3-save batch is lean screening for CPU/GI,
not READY_FOR_LIVE qualification. No gates are relaxed: original contracts are embedded
in predicted.json. Full CPU camp/energy/reference/OMM-fallback coverage and float GI
transient evidence remain pending. Frame time is recorded, never judged unfocused.
M4 retains its existing gates; all items stay off. No live result or GPU-cost claim.

Pending inventory: [other ready items](../tools/live_batches/qual_stage4/pending_items.md).
Unmerged tips: hair groom 3, wf9 1, hm-camp-grass 1, release/long-session 4; old guards 8 superseded.
All other audited owner tips have empty main..branch logs. Review unmerged commits; do not merge blindly.
Dryrun: three synthetic jobs, scene-wait/capacity lints, 714 s phases / 630 MB estimate.
