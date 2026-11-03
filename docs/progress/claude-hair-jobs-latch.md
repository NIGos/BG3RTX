## Hair live jobs for the latched strand key (branch claude/hair-jobs-latch, 2026-11-03)
- main 8df7978d latches rt_hair_groom_primary at device creation (no live enabling for users). The hair jobs now set
  it at startup (startup_only in live_flow) and A/B with rt_hair_groom live: off = native cards everywhere, on =
  strands in RT and on screen. hair_crowd lost its RT-only window (no live mode for it any more). hv5's stages:
  native, + physical origin, + strands (groom), + fibre pass; both strand stages need strand draws in their 25 s
  survival windows. Warm-ups are never judged (hc1: a build gate on warm-back stopped the run).
- live_flow: hair_crowd 2 jobs 0 failing (dryrun passes, 11.7 min, 1.8 GB), hair_validation 5 jobs 0 failing.
  hair_validation's lean dryrun still flags phases below 5 s that main's tools add (tighten's 1.1 s restore,
  producer-sanity 1.1 s phases): a conflict between live_flow tighten and lean_dryrun, not these jobs.
