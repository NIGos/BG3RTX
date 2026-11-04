# Dialogue3 paired result 032335: FAIL for Ultra

VALID runner, 648 s, 87/87 phase serials/settings verified; 10 counterbalanced pairs, no game launch. User reports visibly steadier DoF.
Primary family: exact one-sided sign test; ON minus OFF, ties lose; alpha = 0.05/12 = 0.00416667. T/E/O = total/even/odd, all residuals x 1000.
Full paired differences for every T/E/O observable, controls, components and float energy are retained in [JSON](RESULT_pairs_032335.json).

| Observable | Total paired differences (pairs 00?09) | p (T / E / O) | Bonferroni |
|---|---|---|---|
| rendered/hair_over_dof | +13.228, -1.298, +7.606, +1.774, -0.223, -3.348, +25.811, +1.400, -0.550, -0.903 | 0.623047 / 0.623047 / 0.623047 | FAIL |
| rendered/bright_contour | +5.244, -8.299, +6.035, -0.210, -0.311, -0.990, +20.001, +0.177, +0.845, -0.022 | 0.623047 / 0.828125 / 0.623047 | FAIL |
| presented/hair_over_dof | +0.692, +7.026, +15.505, -8.860, -3.711, +2.969, +5.657, -0.926, -6.055, -8.995 | 0.623047 / 0.623047 / 0.623047 | FAIL |
| presented/bright_contour | -0.465, -0.261, +2.631, -5.037, -1.256, +0.093, +4.880, -0.543, -6.513, -5.227 | 0.171875 / 0.376953 / 0.171875 | FAIL |
| rendered/hair_box | +12.946, -1.747, +7.119, +1.356, -0.059, -3.757, +25.302, +1.395, +0.455, -1.185 | 0.828125 / 0.945312 / 0.828125 | FAIL |
| rendered/far_background | +0.079, -0.008, +0.051, -0.009, +0.030, +0.065, +0.128, -0.019, +0.056, -0.013 | 0.828125 / 0.828125 / 0.945312 | FAIL |
| presented/hair_box | +0.604, +6.748, +15.312, -8.778, -4.586, +3.018, +6.385, -0.727, -6.168, -9.191 | 0.623047 / 0.623047 / 0.623047 | FAIL |
| presented/far_background | -0.012, -0.028, +0.106, -0.086, -0.481, -0.021, +0.248, +0.122, -0.357, -0.054 | 0.171875 / 0.171875 / 0.171875 | FAIL |
Hair-box/far-background sign tests are diagnostics at the same threshold, outside the predeclared family; they cannot substitute for its 12 tests.
Registration: rendered 20/20; presented 9/20 cross-window failures. Primary tests improve only 4?7/10 pairs, versus 10/10 required.
Face control: FAIL in both domains, 0 frozen pixels; differences and p unavailable. Transport: PASS.
Energy (pre-post scene dumps.json float mean): FAIL; mean paired delta +0.000455770 (+0.1492%); OFF spread 0.00701838; on-00 fails; darkening-sign diagnostic p=0.828125 (FAIL at Bonferroni threshold).
Fixed far-background chain float mean: FAIL registration; numerical mean paired delta +0.000488043 (+0.0501%); darkening-sign p=0.0546875 (FAIL). Neither sign test proves equivalence.
Cost: PASS, isolated active temporal 0.152/0.153 ms (median 0.1525); chain 0.164 -> 0.164, grade 0.059 -> 0.092 ms. Paired cost/sign p: N/A, one ON block.
Ultra does not qualify: residual, registration, face and energy fail; independent S8 quality remains missing. Keep off/Advanced; no preset/defaults/cost metadata promotion.
Cleanup: deleted exactly 940 analyzed raw/float/presented files (4,560,799,306 bytes); exact paths/sizes in JSON deleted/cleanup_manifest. No other references in 49 worktrees or shared pending/running inboxes; metadata/unread files retained.
Validation: paired test suite 9/9; committed analyzer completed after numeric presented-frame sorting fixed p96?p103. No commits or git metadata writes.
