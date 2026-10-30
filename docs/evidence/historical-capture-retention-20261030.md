# Historical capture classification — read-only, 2026-10-30

No files in captures/shared trees were written, deleted, consolidated or newly
hashed. No junction was traversed. This does not release an existing owner hold.
The companion JSON lists all 52 exact protected capture directories, their
existing-audit logical byte estimates, source reports and representative raw paths.
All other historical raw data remain unclassified and held.

| Batch | Exact scope | Logical bytes | Classification |
|---|---|---:|---|
| en2 static 301 | 36 captures explicitly listed by `p1-normal-footprint-static-301.json` | 14,595,336,505 | Keep all 18 off and 18 on samples |
| en2 pan 303 | `captures/20261029-085459` | 15,448,654 | Keep failed moving diagnostic |
| pan-motion private smoke control | `20261028-124945`, `-125002`, `-125020` | 907,315,924 | Keep baseline/export control |
| pan-motion private S14 sequence | 12 captures from `20261028-131006` through `-131336`, exact list in JSON | 3,681,338,887 | Keep candidate/export failure context |

These bounded sets total **19,199,439,970 bytes (17.88 GiB)**. Estimates reuse
`docs/evidence/disk-usage-20261030.json`; they are logical pathname sizes, not
fresh physical-allocation measurements or a claim about disk growth.

## en2: decisive failure/control evidence

Root: `C:/Users/quali/.codex/worktrees/en2-selfcheck/BG3RTX`.
`PAUSED.md` and `docs/GI_NORMAL_FOOTPRINT.md` retain an incomplete P1 candidate.
Static 301 passed finite/alpha/predicate checks, but original-PRE energy changed
by +0.4599% with an interval excluding zero. Its on/off +0.6464% within observed
noise is not an energy pass. The reported 36-sample estimator requires its
baseline, candidate and noise samples; retaining a pleasing pair would change
the evidence. Preserve every listed directory and its raw PRE/POST, origin,
pose, saved alpha, run/frame/dump metadata and original reports/logs.

Representative raw path:
`captures/20261029-083705/img/d184_checkpoint_gi.surface_legacy_private39_RGBA16F.f32`.
The same directory's `gi.trace`, `gi.temporal`, `gi.fresh_gate_off`, `gi.origin`
and `gi.surface_pose` raw checkpoints must remain, alongside their counterparts
in all other 35 samples. PNG previews cannot reproduce exact float/alpha tests.

Pan 303 ran at verified 8 fps and still increased raw residual by 21.951% and
cohort energy by 11.193%; no quality gain was demonstrated. The low frame rate
does not make this failed diagnostic disposable. Preserve the whole capture
and the log named by `docs/evidence/p1-normal-footprint-pan-303.json`.

## pan-motion-vectors: completeness is not quality or safety

Root: `C:/Users/quali/.codex/worktrees/pan-motion-vectors/BG3RTX`.
Main `PROGRESS_CODEX.md` explicitly retains this research. The selected baseline
is `captures/private-smoke-report.json`; the candidate is
`captures/private-S14-sequence-report.json`. Their comparison is
`captures/private-sequence-validation-compare.json`.

The reports prove checkpoint export completeness, not valid source pixels,
writer meaning or universal resource lifetime. Both validation logs include
`VUID-vkCmdCopyBuffer-srcBuffer-00118` plus other unresolved diagnostics; “no new
IDs” does not establish correct data. Preserve all 3 baseline and 12 candidate
capture directories, six raw checkpoints per capture (`gi.origin`, `gi.trace`,
`gi.temporal`, `gi.ssao_original`, `sun.trace`, `sun.resolve`) and their logs.
The reports declare 3,933,388,800 bytes for these 90 raw checkpoints alone.

Representative baseline raw path:
`captures/20261028-124945/img/d145_checkpoint_gi.trace_private39_RGBA16F.f32`.
Candidate counterpart:
`captures/20261028-131006/img/d143_checkpoint_gi.trace_private39_RGBA16F.f32`.
No raw file is released merely because the report has `complete: true`.

## Verified redundant storage, preserving evidence paths

The existing `disk-identical-png-aliases-20261030.json` contains 2,675 verified
byte-identical PNG alias groups, all in en2, with **1,568,070,001 redundant bytes
(1.46 GiB)**. It covers none of the 52 directories selected above and contains
no pan-motion group. No additional hash scan was run.

Example: in en2 `captures/20261028-081712/img/`,
`d93_p0_c0_i11279_R16F.png` is byte-identical to the `d94_p0_c1`, `d95_p0_c2`
and `d96_p0_c3` aliases named in the manifest. Their 102,567 redundant bytes are
content redundancy, not permission to break the names referenced by dumps.json.
Future owner-approved immutable hard-link consolidation could retain all names
and bytes. It requires writer/liveness checks and does not relax raw holds.

**Approved raw disposal: 0 bytes.** No analyzed repeat was proven safe to discard
as a raw dataset in this bounded review. The remaining roughly 177 GiB capture
collections need further batch-specific owner classification; this is not a
claim that the unreviewed remainder is disposable or that all 1.46 GiB can be
reclaimed immediately. Preserve PAUSED/failure/control sets and current renderer
holds independently of any future lossless PNG consolidation.
