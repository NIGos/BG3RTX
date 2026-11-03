# Hair validation evidence contract

This is a fail-closed offline analyzer and prepared live regression batch. It
does not claim the current dump transport can supply every requested observable.
See `docs/HAIR_VALIDATION.md` for the missing runtime-owned data. No game launch
is authorized by generating jobs or by passing a fake-queue dryrun.

`RUN.json` uses paths relative to itself and these fields:

```json
{
  "run_id": "unique queue run ID",
  "game_log": "run.bg3rtx.log", "runner_log": "run.runner.log",
  "validation_log": "validation.log",
  "pipeline_identifiers": ["hair_primary", "our exact debug pipeline handle"],
  "required_windows": ["shot-strands-a", "shot-strands-b"],
  "observed": {"characters_enter_leave": false, "lod_transitions": false,
               "repeated_toggles": false, "smoke_ultra": false},
  "windows": [], "performance": []
}
```

The required inventory comes from the predicted job, including both ON windows,
all observed LODs and post-reload frames. `observed` entries require reviewed log
and capture provenance; merely having a phase named zoom is not evidence of LOD.
No pass is possible with the example's empty arrays/false observations.

Each `windows` entry has `name`, `run_id`, `registered`, `arrays` (NPZ path),
`depth_precision_m`, `linear_quantum`, `code_quantum` and `repeats`.
Repeats contain >=3 independent stationary OFF/OFF metric samples for
`coverage_loss`, `iou_loss`, `energy_delta`, optional `pt_error`, and separate
`rendered`/`presented` dictionaries each with `reprojection`, `trailing`, `flicker`.
Noise is assumed stationary Gaussian for Student-t bounds; animation/camera cuts
must be registered/excluded before measuring repeats. Do not use three copies of
one image or correlated adjacent pixels as independent samples.

NPZ arrays: baseline `character_ids` (positive stable IDs; zero background),
`cards` (baseline hair support from material class 5/hair mask), `strands`
(visible candidate strand mask), `visible_cards` (candidate native card mask),
`strand_depth`, `scalp_depth`, `footprint_m` (depth tolerance contribution),
`cards_linear`, `strands_linear` (pre-post scene luminance), optional `pt`.
Masks must have identical view/resolution; exclude occlusion identically.
Never derive strands solely from G-buffer class 5: native cards share that class.
Missing player hair must remain in the baseline inventory.

For each of `rendered` and `presented`, provide `_frames` (N,H,W,3 PQ codes),
`_masks` (N,H,W hair support), `_character_ids` (H,W), `_mv` (N-1,H,W,2),
`_disoccluded` (N-1,H,W), `_fresh` (fresh current reference, no history),
`_registered` (registered frames for residuals). N >=3, no cuts, same-run only.
MV units are **backward render/output pixels**, respectively: sample previous
at current pixel plus MV. Decode scale/Y sign explicitly. Out-of-image support
is excluded, never wrapped. Generated presents need their own proven MV mapping.
Extract mask/depth/material from `dumps.json`/pan samples using `scripts/qa/decode.py`;
retain raw precision and material bits. Previews alone cannot certify this input.

PT images should come from a matched replay pack using `frame_replay --pt-reference`
(`tools/frame_replay.md`, `tools/pt_reference.hlsl`), with camera, materials,
lighting, hair geometry/BSDF and exposure identical. The generic replay PT does
not automatically certify the new strand BSDF; absent matched hair PT is INVALID.

Performance entries: `characters` (observed 1/5/10), `window`, `exclusive`,
`diagnostics`, `contention`, `frame_ms` (>=100 actual rendered samples),
`frame_budget_ms` (=1000/declared target Hz), `hair_gpu_ms` from existing hair
cost lines, `off_gpu_ms` (>=3 repeat samples), `timer_quantum_ms`, `vram_mb`,
`device_budget_mb`. Cost gates permit no statistically significant regression
against OFF repeats; brightness gates must also pass. Amortized GPU ms/count
is explicitly labelled, never presented as isolated per-character timing.

The budget is <=2 GB: 15 compact traces at estimated 30 MB, six bounded edge
K8/P16 pairs at <=165 MB each, plus logs. Inspect measured size before staging.
Half-size edge captures/960-wide previews cannot qualify subpixel silhouettes.
Keep summaries and inputs still in use; do not silently discard failed windows.
