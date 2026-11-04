# Current Sun and alpha spill release diagnostics

Prepared only. No game launch or live authorization. Run S8 diagnostics beside
the S8 water group; retain separate launches where combined duration exceeds
15 minutes. Regenerate with `python -m tools.live_batches.backlog_diagnostics.make_jobs`.
Validate with `live_flow.py tools/live_batches/backlog_diagnostics --preflight`
and `dryrun.ps1`. Decisions are in predicted.json; lint success is no live pass.

Current Sun is restart-latched and stays ON throughout cs1. The shader predicate
is `shaders/rt/rt_common.hlsli:262`; existing aggregate counters are emitted by
`core/sun_trust.cpp:259` every 600 frames. The two diagnostic windows each must
contain a complete post-settling report with real rays and zero scene-unready
pixels. The historical 368,956-ray sample followed by all-unready windows does
not meet this rule. Positive unready rejects release inclusion; it does not
identify a failed predicate clause. A renderer owner must expose all six clause
bits before proposing a guard fix. Do not relax the predicate to obtain samples.

Alpha spill uses S8 because the retained sw1 log at 21:30:41.429 reports
0.61 alpha draws/frame made opaque, opaque loss 0.00, at 1024 MB. This is an
observed overflow scene, not an artificial buffer reduction. Peak-demand fields
are lifetime peaks and do not themselves establish current overflow. Both
OFF controls must reproduce nonzero substitutions. ON must rescue nonzero
draws, preserve direct alpha records and have zero residual substitutions.
`layer/geocapture.cpp:2390` emits rescue lines per 120-frame interval only when
rescue occurs; silence cannot count as zero. Cost windows keep diagnostics OFF.

Use existing `tools/m4_promotion_gate.py` (Run and scene-mean reader) for traces and
float dumps.json, `scripts/qa/decode.py` for float layers, and
`tools/live_batches/dialogue_shadows/pair_registration.py` for registration. Compare static OFF/ON/OFF pre-post float means against
the measured OFF-repeat range, inspect affected cutout edges and unaffected
regions separately, and report the RT GPU ms range and delta. Alpha semantics
can brighten incorrectly occluded regions; whole-frame energy identity is not
the shading correctness gate. Missing attributable masks or captures is
unproven, never promotion. Record numbers and disposition before release.
