"""Sensible gates for in-run A/B analyzers (AGENTS.md "Measuring correctly", user rule 2026-11-14).

A difference FAILS only when it is beyond the run's own noise (the OFF repeats) PLUS a materiality floor, in the bad
direction: |ON - OFF| > noise + floor. The noise is what the OFF repeats alone can move the number by, the floor is the
smallest change that matters; a real change that matters is the two added, not the larger of them (hq-inn-fix 211816:
RT +0.130 ms against noise 0.095 and floor 0.113 "failed" by 0.017 ms on an 11.3 ms pass). Beyond the noise and within
noise + floor it is INFO (the number is printed, never FAIL or INVALID). A change beyond noise + floor in the good
direction that the design does not predict is EXPLAIN (possibly skipped work): listed at the top, not passed silently.
A tail statistic (frame time p99: about the second worst frame of a 120-frame period) takes the full range of the OFF
periods as its noise, not half of it. Event counters the feature owns (fallbacks, mismatches) stay at zero: one event
is a fault. Events the baseline also has (scene wait timeouts) fail only when the ON windows count more than the OFF
windows of the same run; in an OFF window they are the baseline, reported as INFO. A window without enough complete
periods is "not judged"; a run is INVALID only when no decisive window can be judged.

Floors (derived, stated in every report by `floors_text`):
- cost and frame time: the larger of 0.1 ms and 1% of the OFF value (0.1 ms is the resolution of the 120-frame GPU
  medians between OFF repeats of one run; 1% of a 16.7 ms frame is 0.17 ms, under one frame in a hundred).
- energy (mean scene colour): 0.5%. The just-noticeable luminance difference between adjacent patches is about 1%
  (Weber fraction in photopic viewing); a uniform change of the whole frame is harder to see than that, so half of it
  is conservative. It is also below one 10-bit PQ code step (about 0.9% of luminance at these levels), so the
  presented frame can move by one code at most.
- match percentages: 1 point (a one-frame-old scene costs tens of points: TRACK_D, 33% against 97%).
- counts that should be equal: 1% of the OFF value, at least 1 (so the tolerance is the OFF spread plus 1%).
"""
OK, INFO, EXPLAIN, FAIL = "ok", "INFO", "EXPLAIN", "FAIL"
COST_FLOOR_MS, COST_FLOOR_REL = 0.1, 0.01
ENERGY_FLOOR_REL = 0.005
MATCH_FLOOR_POINTS = 1.0


def cost_floor(off):
    return max(COST_FLOOR_MS, COST_FLOOR_REL * abs(off))


def count_floor(off):
    return max(1.0, 0.01 * abs(off))


def floors_text():
    return (f"Materiality floors: cost and frame time max({COST_FLOOR_MS} ms, {COST_FLOOR_REL:.0%} of OFF); energy "
            f"{ENERGY_FLOOR_REL:.1%} of the mean scene colour (half the about 1% just-noticeable luminance difference, and "
            f"under one 10-bit PQ code step); match {MATCH_FLOOR_POINTS:g} point; equal counts 1% of OFF (at least 1). "
            "FAIL = beyond the OFF repeats' noise plus the floor, in the bad direction. EXPLAIN = beyond noise plus "
            "floor in a direction the design does not predict, or an energy difference beyond noise under the floor. "
            "INFO = beyond the noise, within noise plus floor; or an event the OFF windows have too (baseline).")


def events(on, off_max):
    """Status of an event count in an ON window against the worst OFF window of the same run: none ok; no more than
    the baseline has INFO; more FAIL. off_max 0 (or a counter that only exists ON): one event is a fault."""
    return OK if on <= 0 else (INFO if on <= off_max else FAIL)


def compare(off_a, off_b, on, floor, rule, predicted=False, off_values=(), tail=False):
    """(status, delta, noise) of ON against the OFF pair. rule: "lower" (the deliverable: a rise is bad, a drop is
    predicted), "not higher" (a cost: a rise is bad, a drop needs an explanation unless `predicted`), "not lower"
    (a rise is fine), "equal" (a count: either direction needs an explanation). off_values: every OFF period value;
    the noise is then the larger of the pair difference and half their range (tail: a p99, their full range).
    Beyond the noise and within noise + floor: INFO."""
    noise, delta = abs(off_b - off_a), on - (off_a + off_b) / 2
    if off_values:   # the single OFF periods: ON inside their range is no difference (as the energy rule)
        noise = max(noise, (max(off_values) - min(off_values)) / (1 if tail else 2))
        if min(off_values) <= on <= max(off_values):
            return OK, delta, noise
    if abs(delta) <= noise:
        return OK, delta, noise
    if abs(delta) <= noise + floor:
        return INFO, delta, noise
    if rule == "equal":
        return EXPLAIN, delta, noise
    bad = delta < 0 if rule == "not lower" else delta > 0
    if bad:
        return FAIL, delta, noise
    return (OK if rule in ("lower", "not lower") or predicted else EXPLAIN), delta, noise


def energy(off_samples, mean, predicted=False):
    """(status, relative delta) of a state's mean against the OFF samples: inside their range ok; outside it and
    under the visibility floor EXPLAIN (real, not visible: find the cause); beyond the floor FAIL. `predicted`: a
    difference the run was built to reproduce is reported as INFO."""
    off_mean = sum(off_samples) / len(off_samples)
    rel = mean / off_mean - 1
    if min(off_samples) <= mean <= max(off_samples):
        return OK, rel
    if predicted:
        return INFO, rel
    return (EXPLAIN if abs(rel) <= ENERGY_FLOOR_REL else FAIL), rel


def split_windows(labels, periods, required, minimum=1):
    """(judged labels, ["label: window not judged (...)"]) from periods[label][name] = complete-period values."""
    judged, notes = [], []
    for label in labels:
        short = [n for n in required(label) if len(periods.get(label, {}).get(n, [])) < minimum]
        if short:
            notes.append(f"{label}: window not judged (fewer than {minimum} complete period(s) of {', '.join(short)})")
        else:
            judged.append(label)
    return judged, notes
