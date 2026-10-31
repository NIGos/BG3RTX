# Pending merges — 2026-10-28

Phase branch: codex/p2-pending, based on main 6dc153e.

## Frozen local-light reference: accepted

Selectively cherry-picked 2f51e8c as 93bb66f. The conflict was resolved by
preserving current main's tri-state visibility and configurable candidate cap,
while importing the missing shadow-ray class, scene guard and list bounds.
The production local-light pass and its default settings are unchanged.

Fresh S6 hold-90 queue runs on ef7c170 are both valid, with no device loss,
dumps or build changes. Every complete reference window has zero any-bit,
relative-error and one-writer-only differences:

| Job | Seconds | Windows | Reference-written pixels |
|---|---:|---:|---:|
| p2-lscheck-current-cap0-h90-r2 | 103 | 25 | 8,172,707,324 |
| p2-lscheck-current-cap1-h90-r2 | 104 | 25 | 8,172,707,294 |

Logs are under main's captures/queue/logs, ending 184221 and 184404 respectively.
The first two refresh jobs were marked invalid because their session-slice
expect incorrectly required the earlier startup asset-DB message. They are not
used as deciding runs. The full process log independently confirms the real
8.0 MB asset DB loaded at 18:38:18.550; the valid repeats check their own windows.

Offline checks: full host/core build, 3/3 registered CTests, 32/32 shader files
including all declared compute entries, and QA detector tests pass.
build.ps1 -Test now runs all CTests and rejects an empty suite. merge-green
uses the shared shader compiler, checks merge errors and main ownership, and
aborts its own failed merge. Disposable Git fixtures pass invalid-ref, build,
shader and QA failure cases, successful merge, and already-merged cases.

## EN2: not ready

Pure S8 windows join 300/300 frames without waits or timeouts, but blue,
metalness and luminance bias still fail. Bias Y is -0.0030643 / -0.0032121.
Keep the feature off; investigate families 613ead9c and 14678345 and the
class-mismatched samples. A join pass is not a material-correctness pass.

## D-EM: not ready

AF family 304be1c7 repeats ratios 0.361 / 0.362 outside the derived
0.842–1.187 band (237 bright pixels). GF passes at 0.999 within 0.979–1.022
(12,487 pixels). Keep off pending the AF discrepancy; isolated captures
20261028-010204 / 010228 exist on its feature tree.

P1 remains the active rendering priority while this independent reference fix
is integrated. No EN2, D-EM, box-light, OMM or GI candidate is approved here.


## Resumed P2 checkpoint — 2026-10-29

The required P1 fresh fallback, normal bounds and static/same-run pan measures
are recorded through 303. No visible gain is established; the GI candidate stays
off. P1 remains incomplete while pending integrations resume under the goal's
stalled-phase rule. The overall goal remains active.

D-EM diagnostic 295 rejects coincident additive draws in the observed AF cohort:
67,031 receivers, no multi-draw hits, native blend disabled. The exact family
still fails 0.362 against 0.842-1.188. No calibration or scale is permitted.
Next inspect facing: native back-face culling versus two-sided emissive lookup.
Preserved evidence is on codex/p2-emissive at 1922260. Future stats jobs use hold
at least 90. The short diagnostic and scoped VVL check are not promotion gates.
