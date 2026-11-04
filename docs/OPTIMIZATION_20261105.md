# Optimization profiling, 2026-11-05

Status: READY_FOR_LIVE. No measurements yet. Rendering and shader requests are unchanged.
Batch: `tools/live_batches/opt_profile/jobs/opt-profile-*.json` (four launches).
Expected total 17.1 minutes including 120 s load per scene; 20 minutes total hard caps.

Each log's post-analysis writes `<log>.profile.md` with rows for this table. Paste only
reviewed rows after the authorized batch, with the log and runner paths alongside them.
Keep Ultra/High and static/pan separate. Those views are not matched-camera A/B evidence.
Report median, min/max and number of complete period summaries for every pass.
Do not add GPU medians or subtract GI trace from GI total to invent a denoiser timer.
Release GI denoising belongs to shared RR; CPU submit/driver are watch interval totals.
FG copy's first straddling line is discarded. Missing or disabled work is never zero.

| Scene | Preset | Window | Pass | Median | Range | Periods | Unit | Status |
| --- | --- | --- | --- | ---: | --- | ---: | --- | --- |

Pending measurements: scene build; GI total/trace/shared RR denoise; local shadows;
reflections; water build/reflect/compose; RR; post chain/grade/temporal; FG copy;
emissive build; CPU submit; watch driver time. See the batch README for units and gates.
