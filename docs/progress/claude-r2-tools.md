## Round 2, stream C: gi_far_roof_emission key, equivalence gate robustness, dlss5 lint, release docs, verify_rc version (branch claude/r2-tools, offline, 2026-11-20)

- `gi_far_roof_emission=0` is now a raw developer key (bg3rtx.ini after `gi_far_roof_early`, generated defaults,
  settings_test release-default list, pipeline_readiness: the GI trace family, spec constant 1054).
  Staged, not queued: `tools/live_batches_staged/r2_tools/` r2-inn-roof-em (inn, Ultra, FG 3x, unfocused, static:
  key 0 / 1 / 0, cost and energy windows, then one GI replay pack for the offline PT reference). predicted.json
  first. Frozen and live lint 1 job, 0 failing; snapshot append only.
- `release_shader_equivalence.py`: the run of 2026-11-20 had no error text because a crashed spirv-opt prints
  nothing and the exception ended the gate. Now `spirv_equivalence.OptError` names the stage, passes and exit code
  (NTSTATUS: 0xC0000005 access violation and others); a failed pair is retried once with one optimizer round on
  both sides (still a proof), recorded as a fallback; otherwise the request is ERROR with entry, assignment, side
  and the kept module (`<cache>/release_equivalence/failed/`), and the other requests run on. A dead worker names
  its request. ERROR exits 1. Default schedule and receipt keys unchanged. CTest `release_shader_equivalence_test`.
- Why it ran 30 minutes: against the worktree the NaN clamp (3d7c8a99) makes all 8 hit_sun_em requests really
  differ, so each failed universal check fell into its 2^13 Cartesian cross (ETA 13 h). The first difference now
  decides a request (without `--exhaustive`): same verdict in 74 s. `--current REV` compares a commit: 163e70bc
  against its parent with 1054 frozen to 0: 6 equivalent (universal), 2 identical, control PASS, 248 s.
  The spirv-opt crash itself did not recur in about 25 minutes of the old cross; the gate now names it if it does.
- Staged dlss5 job (untracked since 2026-11-05) committed: PNG windows 10.5 s (8.1 s held no complete trace), its
  6 readiness queries and the NR GPU pattern appended to the frozen snapshot. Frozen and live lint 0 failing.
- verify_rc: the Setup skip reads the install's `VERSION.txt` (hairtest18) and names a different Apps
  DisplayVersion beside it (script_guards_test). Note: `reg.exe` in the Bash sandbox reads DisplayVersion
  hairtest10, PowerShell reads hairtest18: the RC's name came from such a view; the file read is robust.
- Docs: frame_replay.md and TRACK_C_GI no longer document `--define GI_FAR_M` (now `--push radius=6`); no tool
  uses GI_FAR_M, GI_DEBUG_SWIZZLE or id 1012. RC_CHECK readiness refreshed for e2770961 (candidate 745a1fff, zip
  910c03b6, setup 22da20f2; three new keys off; not-verified 13-15). Release notes, Nexus changelog, evidence:
  the groom decode fix (offline) and 15-50 s of strand-variant pipeline creation at start, also with RTX Hair off.
- Tooling: `perf_ab/make_jobs.py` holds `energy_phase` and `write_made` (4 and 5 copies before; energy phases
  byte-identical). merge_scope's PYTHONPATH (stream A) matches CTest; merge_scope_test asserts it. Staged README
  states; no batch sets only removed keys, so none archived (perf-ab-wide-split is obsolete in purpose, kept).
- Left: the r2-inn-roof-em live (needs a user go); `.scratch/setup-0.9.6-beta/SHA256SUMS.txt` still lists the
  976df3df hashes; the analyzers' window loops (queue_gates helpers already shared, the rest differs per batch).
