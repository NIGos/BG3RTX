## RTX Hair: scene pass header confirmed and into the On bundle, hitch frames fixed, key rt_hair_coverage_keep (branch claude/hair-adopt, READY_FOR_LIVE, 2026-11-14)

Design, table and evidence: docs/RTX_HAIR_PRIMARY.md 6i. Offline only; live hx1 190905 (VALID) analysed, no new live.

- hx1 190905, key off (rt_hair_header_scene_pass 0): the decal pass took the label in 31-42 of 60 frames while still
  and 165 of 217 / 131 of 183 in the two dollies (52-76% of all frames traced the publish before). in-base: 6 frame
  watchdog trips, 9 switches to cards, 29 of 181 frames with every draw on cards; out-base: 3, 12, 29. Dense samples
  moving: accepted 61-71%, no strand 16-25%. All 8 watchdog lines of the run are in these two windows.
- Key on, 24 period lines (still, 7 moving windows): label taken by the decal pass 0, by another scene pass 0, frame
  and key watchdog 0, coverage low 0, coverage high 0, 0 frames on cards, dropouts 0, own-bound fails 0.
  Card fill off (in-fix, out-fix): 0 switches to cards, 181 of 181 hair frames with strands, moving accepted 81%,
  no strand 4.0-4.1% (still 81-85%, 4.9-5.0%): the loss under motion is gone. No hitch frame in the run.
- The 3 strands->cards switches left with the key (pre-in, in-fill, out-fill) are all "coverage stale" of key 22a2:
  no card pixel in the counted frames after the dolly in, stale 28 frames later, cards unseen. Visible is only the way
  back: a head that comes into view again shows cards for 8-10 frames (2-3 cards->strands per dolly, card runs of 10).
- Gates quiet with the key: the worst used ratio of a swapped draw stayed 0.017-0.097 above its own bound. No
  threshold changed. 6e's "same build" reading corrected in 6i (the stamp was copied at the decal pass's apply).
- Adopted: RTX Hair On writes `rt_hair_header_scene_pass=1` (`Choice::kv` 24 -> 32; old files get it on load).
- Fixed: two scene G-buffer pass recordings in one present interval. The scene build runs once per label, so both
  trace the same publish; the second now applies the header again with the next tag (never a tag twice), its own
  pending header and GbFrame (`hair_primary::HeaderLabels`, `HeaderPass::Again`). Counter on the header pass line:
  "second scene passes of an interval with their own header and tag".
- New, off: `rt_hair_coverage_keep` (Advanced knob "Strand hair: keep the last coverage verdict", live, CPU only):
  coverage evidence does not expire by age; a newer measurement, an invalidation, a latch or a watchdog still replace
  it, a new key and every key after a level load still need a measurement. Not in the bundle (a gate change, no live).
- Proof offline: CTest hair_groom_asset_test test_header_pass (two and three frames in one interval: key off 1 frame
  on the other's header and its entries reused; key on 0, tags unique, tags meet the labels again) and
  test_coverage_keep (stale after exactly 28 hidden frames without the key, never with it; failed, invalidated and
  new keys never passed). No shader, no ABI, variants.txt delta 0, GPU cost 0. build.ps1 -Test 226 of 226.
- hx1 regenerated (predicted.json first): 25 windows, about 140 s. Bundle alone, card fill flag off, in 2 dollies,
  2 orbits (180 frames, turning; the old orbit windows turned for a second), 2 short dollies and 2 still windows:
  header at the scene pass only, decal 0, another scene pass 0, every gate's switch count 0 (coverage stale open),
  0 frames on cards, 0 trips. Then keep on (0 switches of any cause), card fill on (PNG pairs, still and a second
  after the dolly, at 2560), control without the key (decal 1 or more). Lint live and frozen 0 failing, snapshot
  appended only, merge_scope tooling 0.
- User check after a go: S8, RTX Hair On alone: zoom fast with the wheel and orbit; far and edge heads keep strands.
- Open: card fill decision (user, the PNG pairs); `rt_hair_coverage_keep` into the bundle if its windows hold;
  styles without a groom file keep cards (S8: 3399 and 7086 indices). Removable after the live: rt_hair_stale_publish,
  rt_hair_row_stable, the per-draw and one-key watchdogs, the swap latch, the key-off header path.
