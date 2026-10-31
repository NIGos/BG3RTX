# Test artifact storage and cleanup coordination

Superseded: the cleanup was done on 2026-10-31 (about 222 GB freed: old captures, worktrees, Temp); kept for history.

2026-10-30. User requested identifying genuinely disposable test data and informing
the active agent. No general permission to remove evidence or checkouts is implied.

## Measured inventory

Read-only traversal excluded NTFS reparse points/junctions. Logical sizes by path,
not a byte-for-byte allocation audit; no historical free-space baseline exists.
C: had about 329 GiB free at the initial audit.

| Area | Captures | Other relevant observation |
|---|---:|---|
| .codex/worktrees/en2-selfcheck/BG3RTX | 128.13 GiB | 104.20 GiB .f32 in the whole checkout; 369 dated capture folders |
| .codex/worktrees/pan-motion-vectors/BG3RTX | 49.05 GiB | 25.98 GiB .f32; 146 dated capture folders |
| Main .claude/worktrees | 20.60 GiB | All contents about 30.10 GiB |
| Main captures | 17.22 GiB | Includes current dialogue/UI/FG evidence; preserve |

Raw float dumps and PNGs dominate. Temp fixture directories are small and not
the cause of the space loss. Builds/logs are much smaller than raw capture data.

## Immediate protection and ownership

- Do not delete either large worktree or its captures wholesale. en2-selfcheck
  contains PAUSED.md and failed P1 energy/motion evidence. pan-motion-vectors is
  explicitly retained research in PROGRESS_CODEX.md. Branch age is not disposal proof.
- Preserve all current dialogue/hair/scalp/cascade/solar/UI evidence, queue results,
  source logs, evidence JSON, failed/invalid regression cases, representative
  controls and captures needed for pending analysis. Keep originals when previews
  cannot support numeric re-analysis.
- Do not infer disposal safety from absence of a documentation reference, existence
  of a PNG, a job's successful exit or a result.json alone.
- No capture cleanup while another agent is writing/analysing the selected data.
  Coordinator ownership is required for the actual selected dataset, not a global
  pause of implementation. Never clean external/, data/assetdb, backups or saves.
- No recursive deletion/archive of junction-bearing checkouts. Existing AGENTS.md
  protections apply. Prefer exact ordinary-file candidates over deleting trees.

The active task "Risolvi i flash d'ombra durante il pan" was notified on
2026-10-30 of these findings and asked to record required raw datasets below.
Implementation may continue; no live run or concurrent raw cleanup was requested.

## Owner holds

Pending owner classification. Treat the two large P1/research raw collections
as held until their necessary baseline/failure/control sets are identified.
The active coordinator can replace this hold with exact capture/run paths and
reasons, and separately identify redundant analysed repeats. Keep this list short.

Rendering coordinator holds (2026-10-30): preserve main captures/20261030-161807,
-161818, -161845, -162008 and -163004, with their trace/dump/log/alpha_cache inputs:
these are unresolved Hair/Fur/Scalp face and current-Sun evidence. Also preserve
captures/dialogue-shadow-review-20261030/ and captures/alpha_cache/ source recipes.
Keep isolated omm-bake/captures/omm-bake-20261030/ and sun-coverage-20261030/ proof
outputs/reports, and m2-foundry-mesh/captures/m2-foundry/ decoded native assets,
source fingerprints and pinned decoder dependencies while room/material work runs.
No owner release is given for en2-selfcheck or pan-motion-vectors raw collections:
old unresolved P1/lighting baseline/control data have not been classified here.
No raw deletion or cleanup was performed by this coordinator.

## Verified redundant storage

A bounded SHA-256 comparison found 2,675 groups of byte-identical PNG aliases,
with 1,495.4 MiB (about 1.46 GiB) of redundant storage in 324 inspected historical
captures. The scan was limited to 25 seconds / 2 GiB of reads; it is not a census
of all duplicates. No selected file changed during hashing. Exact paths and full
hashes: [disk-identical-png-aliases-20261030.json](../evidence/disk-identical-png-aliases-20261030.json).

These copies store the same content, but their names are still referenced by
dumps.json. Do not simply delete alias paths. A later coordinated consolidation
can preserve every name/content using hard links for immutable snapshots, or
validated manifest redirection if all consumers are accounted for. Do not do
this concurrently with writers or bypass junction protections/owner holds.
No consolidation or deletion was performed by this audit.

44 synthetic queue/policy fixture roots contain 1,901 files totalling 884,048
bytes. Their contents are generated test outputs, not the large-space culprit.
They are regeneration candidates after producer-liveness checks, not evidence
that entire capture collections are disposable. Inventory:
[disk-fixture-candidates-20261030.json](../evidence/disk-fixture-candidates-20261030.json).

## Disposal criteria

1. Exact temporary fixture outputs: removable once their producer has exited and
   they contain no user evidence, unique reports, junctions or tracked source.
2. Generated compiler intermediates: regenerable from a recorded revision and
   existing SDK; remove only from an idle build directory, never an active tree.
   Keep known-good deployment binaries and fault-symbolication PDBs.
3. Repeated capture data: remove only after owner classification and preservation
   of sufficient raw baseline/failure/control data plus reports/metadata. A whole
   QA raw set is not disposable merely because the analyzer offers prune().
4. Byte-identical copies: candidate redundant storage, but path consumers may
   still need both names. Establish equivalence and consumer expectations before
   removing a path; do not silently break manifests.

For every executed cleanup record exact roots/files, reason, bytes and result.
If a selected file changes or is in use, skip it. No age-only deletion of captures.

## Retention for future tests

Before a batch, estimate output size and reuse bounded/selected captures. Prefer
the needed streams, ROI and frame count over dumping every float buffer repeatedly.
Keep complete raw evidence through analysis and owner review. Preserve a small
explicit set of decisive baseline, failure and control captures for unresolved work;
prune redundant analysed repeats only after the checks above. Keep summaries,
run/phases/effective settings, shader/build identities and useful crops.

Use the existing QA machinery; no storage service or new framework is needed.
scripts/qa/analyze.py can delete raw data even after an incomplete analysis result,
so do not run it without --keep-raw to obtain a cleanup decision. Normal reports
do not automatically replace the raw evidence needed by pending questions.

## Audit artifacts

Read-only inventories are currently at:
- %TEMP%/bg3rtx-disk-audit-20261030.json
- %TEMP%/bg3rtx-disk-detail-20261030.json

A durable combined snapshot is in
[disk-usage-20261030.json](../evidence/disk-usage-20261030.json). The raw float alias
scan was stopped without changes to bound I/O; only the bounded PNG scan produced
a duplicate claim. No general full-archive duplicate or recoverable-space claim
is made from file sizes alone.

The coordinator should link any executed cleanup manifest here. No raw deletion
has been performed by this audit as of document creation.
