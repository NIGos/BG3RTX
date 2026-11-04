# BG3RTX progress - 2026-11-04 (main ccbbb1d2 + rc-final)

## Report: backlog closed, release candidate ready for approval

**Question for the user (only one):** approve release candidate rc-20261104-ccbbb1d2-0efe5f84?
Nothing is uploaded. The archive is `.scratch/rc/20261104-182143-0efe5f84/BG3RTX-rc-20261104-ccbbb1d2-0efe5f84.zip`
(72.60 MiB, SHA256 29f1fa9a...8446f, not in Git).

**Done.**
- Every release-backlog row has a final outcome with numbers ([backlog](BACKLOG_20261104.md)).
- Release check PASS ([RC_CHECK](release/RC_CHECK.md)): lean layer without diagnostics, 229 files, delta 0,
  hashes and licences checked, install, update and uninstall on a throwaway folder.
- Final Ultra smoke: rel1 17:52 VALID on the release profile. Later merges (bundle34) touch only tools and docs,
  so no further smoke is needed.

**Shipped in the release profile.**
- Ultra: temporal post (0.152-0.153 ms, user visual verdict), exact-k (S7 0.717 -> 0.555 ms), two-ray GI.
- High: OMM and offline masks (rel2 VALID 12:13).
- Always on: water camera correction wf9 (turning screen hits 6.7% -> 17%, compose 0.154 ms),
  far-GI roof-leak fix (rel1: GI 11.46 ms vs 11.97 ms before, no measurable cost).
- Appearance: native ambient (S8 ratio 1.44), native S8 water shadow edge and glint, hit emission mode 1.
- Ctrl+Shift+F9 master RT toggle; F10 flips the last Experimental choice.
- Licence GPL-3.0-or-later with NVIDIA section 7 exception. "Tested only on RTX 5090".

**Experimental (off in presets).** Water Fresnel (rim +43%, open water +1.8%, invisible to the user on open water).

**Rejected or deferred today, with numbers.**
- One-ray Ultra GI: saves 3.27/3.50 ms GI trace; energy -7.8%/+3.9% inside OFF spread 11.6%; static flicker
  fails in ON-b (PQ excess 0.0032/0.0022 > 0.00098). Rejected for Ultra.
- Native Tilesets: OFF VALID (RT 9.92 ms, GI 12.03 ms); ON INVALID twice for test reasons (no rt_debug=9,
  then the pipeline still building in the 60-frame diag window). Hidden and off; post-release.
- Water off-screen hit shading: compose refused every pass (light binding 21-22). Bits 1/2/4/7 rejected.
- Pak publication: 16,616 unique S6 candidates, 0 published. Generated rows: no coverage gain, +0.3/+0.5 ms.
- Alpha spill (no overflow), three GPU candidates, Medium half resolution, glint, GI guards, current sun,
  IP grass, SHaRC.
- Hair strands: post-release. hv7 16:06 VALID (orphans 0, close-up 0.97); hv9 18:02 fail-fast on 3 orphan
  card depths in one frame. Rule: no hair lives until consolidated offline (review found 9 issues,
  all hair-only, gated by rt_hair_groom).

**Open, not claimed.** Fresh-PC install, signing, Inno installer, Act 2/3 acceptance (the user tests these),
upload.

## Current user decisions

- Stock visible hair ships. Strands are a post-release update on the Experimental page.
- Fresnel Experimental; native ambient and water edge/glint; emission mode 1.
- GPL-3.0-or-later plus NVIDIA exception; no NVIDIA notification.
- Static CRT; no VC runtime ships. Only RTX 5090 tested. Launcher acceptance is DPI only.
- Lean: live windows in frames, one launch per scene, merge-green scaled to changed paths.
- RTX Hair: consolidate and verify offline first; one approved READY_FOR_LIVE job afterwards.

## Today's merges (all pushed)

- bundle32 f051a7bd: short windows, mg-scope, gi1/ts1 jobs, cp5 startup, generated-row fix, water compose
  reasons, hair latch.
- bundle33 a48aaba0: scene-wait traces, water-binding fix, roof-leak fix, docs sync, licences and lean layer,
  hair hv9 job, appearance decisions.
- trace-sizing 6a176bb0: trace windows from the capture-stall model; hv9/ts1 job repairs.
- bundle34 ccbbb1d2: RC verify script, outcomes, gi1 analyzer fix, Tileset job fix.
- History rewrite (user-approved): a 213 MB corpus removed from 17 local branches; corpora now in .gitignore.

## Lessons written down

- Check blobs > 20 MB before every commit/merge; generated corpora never go in Git.
- Counters behind a debug dispatch (rt_debug=9) print zeros otherwise: a gate on them needs that dispatch
  and a window that waits for the pipeline build.
- Tileset and other startup-latched keys need dev=1 in jobs, or the release policy resets them.

Full folded branch notes of today: [archive](archive/PROGRESS_CODEX_2026-11-04.md).
Earlier: [night](archive/PROGRESS_20261104_night.md), [11-01](archive/PROGRESS_CODEX_2026-11-01.md).
