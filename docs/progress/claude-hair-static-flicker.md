## Static-camera strand flicker: diagnosis and staged burst job (branch claude/hair-static-flicker, READY_FOR_LIVE, 2026-11-09)
- User: strands appear and disappear very fast with a still camera; rv3 220145 rules out swapping (strand draws
  720/720, coverage 94-96%, 0 refusals).
- Code read: no jitter, depth or motion-vector mismatch for strand pixels (ray and depth from the jittered vp,
  hair_primary_gbuffer.hlsl:238-245/:374; motion vector = the native card formula, hair_primary_velocity.hlsl:141).
- Most likely: binary per-pixel strand coverage under the jitter (4-6% card pixels without a strand, card-edge pixels:
  the candidate region is the jittered card raster), possibly amplified by RR guides that change per strand (fibre
  normal) and by fg=3. Details: docs/RTX_HAIR_PRIMARY.md 5p. No shader change, no key.
- temporal_burst.py: class-5 toggle rate, stable vs toggling flicker, burst-half noise, RR attenuation (CTest
  hair_groom_test test_burst_coverage_split).
- Staged live (not queued): tools/live_batches_staged/hair_static/jobs/hs1-s8-static-hair.json, one S8 launch, 3
  bursts (strands aa 0, strands aa 2, cards), ~1.7 GB; decides coverage vs reconstruction (predicted.json).
- User check after a fix: Water Queens House, camera still on a party member's hair, no fizzing strands.
