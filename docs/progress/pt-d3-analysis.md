## Dialogue3 post temporal analysis (branch pt-d3-analysis, rejected promotion, 2026-11-03)
- VALID pt-dialogue3.1.210703: rendered bright-contour residual 6.2886/3.5164 OFF -> 0.9347/1.3772 ON, but OFF-noise gate fails.
- Hair OFF 7.5164/33.2349 -> ON 9.6760/1.5008; framing/animation confounding, presented registration rejected, face support empty.
- Float scene means pass OFF spread 0.002971; fixed-background energy cannot qualify without trace registration. Temporal medians 0.1685/0.1725 ms.
- Keep Ultra off; S8 cost 0.12 ms is not the required quality evidence. Check Test Dialogue3 hair over DoF/bright contours; see RESULT_20261103_D3_valid.md/JSON.
- Offline analysis only; no game, git metadata, preset, ABI or shader request changes. Raw deletions recorded in result JSON after reader checks.
- Drift test repaired: 12 adjacent counterbalanced pairs, all-OFF frozen masks; exact sign gate at 0.05 / 12, minimum 8 pairs.
- Derived plan 338.9 s; loading/readiness worst case 818.9 s < 900 s. Dialogue3 live-flow lint passes against 22 valid logs.
- Retained run: INSUFFICIENT (2 pairs); synthetic effect + drift passes, no effect + drift fails. Nine CPU fixtures pass; no promotion.
- Offline only; no game, queue, git metadata or capture deletion. READY_FOR_LIVE test design; evidence still pending.
