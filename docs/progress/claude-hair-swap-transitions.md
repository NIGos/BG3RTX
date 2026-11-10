## RTX Hair strand/card alternation: switch counter in the lean log, retry latch behind a key (branch claude/hair-swap-transitions, offline, not live-verified, 2026-11-10)

- User evidence: hair toggles between strands and cards with a still camera, also with fg=0 (FG ruled out), worse
  far away; zoomed in it flickers, is stable about 0.5 s, repeats. 28 frames (`kWatchdogFrames`) at 58 fps are 0.48 s.
- From the code, two retry loops exist without any scene change: the frame watchdog (28 frames of cards, 2 to 3 to
  resume, verdict 5 to 6 frames later: about 35 to 37 frames per cycle while the fault lasts) and the per-key
  coverage gate (a held key at the minimum ratio fails about 16% of measurements by count noise, then retries on the
  next passing probe: card runs of about 3 frames). The user log has one 31-frame trip pair (2746 -> 2777) and
  near-empty strand frames; the per-trip line is capped at 8, so the period is not proven from it.
- Added, always on, lean too, CPU only: `[hair primary] switches (last 300 frames)` with switches by cause, frames
  with every draw on cards, strand and card run lengths (min / median / max), frame watchdog trip detail and the 3
  worst keys (ratio as strands, as cards, card pixels). `hair_primary::SwapTransitions`.
- Key `rt_hair_swap_latch` (off; launcher "Strand hair: no strand retries", Advanced): a key whose strands measured
  outside its bound keeps its cards until its card pixel count differs from the reference by more than
  sqrt(C) + sqrt(C0). Limit: a noise-only failure stays on cards in a still scene.
- Gates: build.ps1 -Test all pass; `test_swap_transitions` and `test_swap_latch` in hair_groom_asset_test; key off
  runs the old gate answers (same test sequence off and on). No shader or ABI change, variants.txt delta 0.
- To confirm (user session, key off then on in one scene): card runs near 28 to 31 frames with cause "frame
  watchdog", or short card runs with "coverage low"; with the key on, switches near 0 and latched keys > 0.
- Details: docs/RTX_HAIR_PRIMARY.md 5t.
