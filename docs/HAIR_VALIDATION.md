# RTX Hair validation

Status: offline suite; no game launched, no runtime qualification or preset change.
The 17:01 and 17:04 trial logs both fail stability; summaries are in
`tools/live_batches/hair_validation/evidence/`. Runtime repair belongs to its owner.

Prepare: `python -m tools.live_batches.hair_validation.make_job`.
Check: `python tests/hair_validation_test.py`; run the batch's `dryrun.ps1`.
See `tools/live_batches/LIVE_FLOW.md`. All generated jobs stay outside inboxes.
`combine.py` joins phase groups into one job per save: Dialogue1, camp and S8.
Each has static cards/strands repeats, 72 s held Q/E, zoom out/in, six toggles,
K8 rendered/P16 presented pairs, Ultra cost windows, F8 reload and restoration.
F8 requires the runner's selected-save filter; newly sampled loading then two
stable scene samples must occur within 90 s. A timeout fails; live is unverified.
The separate 50 s GPU-AV job uses `-Validation -VSet validate_sync=true,gpuav_enable=true`.
The launcher requires a validation manifest and its DLL in `external/` or
`VULKAN_SDK`; none was found locally or in the parent checkout's `external/`.
It adds the SDK path only to the child environment. No registry installation.

Analyze: `python -m tools.live_batches.hair_validation.analyze RUN.json --out REPORT`.
`summary.json` and `report.md` contain one-line PASS/FAIL/INVALID checks; exit 1
means failed or incomplete evidence. `predicted.json` is written before jobs.
Input contract and limitations: `tools/live_batches/hair_validation/README.md`.

Coverage and silhouette bounds use baseline OFF/OFF disagreement, Student-t
prediction intervals (family alpha 0.05), and one-pixel boundary quantization.
Entirely absent strands always fail. Small unresolved silhouettes are INVALID.
Baseline character IDs include characters that disappear in the candidate.
Depth must be finite, positive and before scalp within depth precision plus
projected footprint. Simultaneous visible cards and strands fail; coplanar
duplicates count as z-fighting risk. Depth is linear metres, not reverse-Z codes.
Temporal checks use backward MV bilinear warping with disocclusion exclusion,
fresh-current references for trails, and registered `edge_flicker.energy`
second-difference residuals. Rendered and presented streams gate separately.
Energy uses character pre-post linear luminance, never PNG or bloom brightness.
Matched PT shadow/backlight images need reference noise repeats; absent is INVALID.
Cost reports observed 1/5/10 cohorts, amortized ms/character (not isolated cost),
driver VRAM budget and rendered p99 against a target-Hz budget declared beforehand.
Every ON window must pass; partial runs and missing captures cannot qualify.

Current evidence gaps: card/strand visibility cannot be distinguished by material
class 5 alone; the runtime owner must expose a proven mask/record dump adapter.
K8 captures have depth/PQ/material/albedo but no certified strand MV or fresh
reference. FG captures lack generated-frame MV provenance. Those gates stay INVALID.
The camp save may not contain matched grooms or exactly 5/10 visible characters;
the suite requires observed count cohorts and never labels total scene time as
measured isolated per-character cost. Extra independent temporal repeats and PT
references must be supplied with the runtime owner's capture support.
No shader requests, ABI, defaults, hair runtime, saves or Git metadata changed.
