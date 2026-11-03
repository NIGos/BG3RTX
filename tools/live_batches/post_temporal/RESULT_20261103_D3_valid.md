# Dialogue3 temporal post: VALID run, qualification FAIL
Run `pt-dialogue3.1.210703`, main `fbce1fec`, Test Dialogue3, Ultra 0/1/0/1; offline only, no game or git writes.
Order below: OFF-a / ON-a / OFF-b / ON-b. Residuals are midpoint absolute PQ residual x 1000.
Gate: each ON and each parity must be below min(OFF) minus absolute OFF-OFF spread; historical targets are not gates.
| Observable | Four window values | Verdict / deciding gate |
|---|---|---|
| Rendered hair-over-DoF | 7.5164 / 9.6760 / 33.2349 / 1.5008 | FAIL; spread 25.7185, threshold -18.2021 |
| Rendered bright contour | 6.2886 / 0.9347 / 3.5164 / 1.3772 | FAIL; spread 2.7722, threshold 0.7441; even parity also fails |
| Rendered hair box | 7.0044 / 8.7823 / 33.8177 / 1.2269 | FAIL; spread 26.8133, threshold -19.8089 |
| Rendered far-background residual | 0.1843 / 0.2108 / 0.2671 / 0.1961 | FAIL; threshold 0.1015 |
| Presented hair-over-DoF | 1.4454 / 2.4554 / 4.6596 / 2.4639 | FAIL; registration rejects ON-a and OFF-b; diagnostic only |
| Presented bright contour | 0.9301 / 0.2092 / 6.1026 / 0.3019 | FAIL; same registration rejection |
| Presented hair box | 1.0030 / 1.8570 / 4.9684 / 2.0594 | FAIL; same registration rejection |
| Presented far-background residual | 0.1125 / 0.1206 / 0.4279 / 0.1121 | FAIL; same registration rejection |
| Depth-stable face, both domains | 0 frozen pixels | FAIL; no valid control support; no threshold retuning |
| Background reconstructed float mean | 0.953993 / 0.966593 / 0.959379 / 0.953010 | FAIL; trace registration unavailable under framing drift |
| Full-image pre-post scene float mean | 0.322697 / 0.323757 / 0.325668 / 0.324959 | PASS; each ON vs each OFF <= 0.002971 OFF spread |
| Temporal cost, timing phases only | ON-a 0.162, 0.175; ON-b 0.170, 0.175 ms | PASS; active=1 in both; medians 0.1685 / 0.1725 ms |
Transport PASS: every rendered K8 has 36 unique finite submitted samples; every presented K8 is consecutive; slots 1/2/6 join to dumps.json.
Components PASS transport/freshness: C, A, bloom and exposure residuals, including parity, are separate in JSON; exposure is exactly 1, residual 0.
For example hair A residual x 1000: 22.8199 / 5.5668 / 30.6798 / 4.1973; C: 15.8425 / 4.2934 / 21.9729 / 2.8061 (linear component units, not PQ).
Shared trace registration rejects ON-a, OFF-b and ON-b; burst-to-trace mask join also rejects. Background numerical OFF spread 0.005386; ON-a vs OFF deltas +0.012599/+0.007214 exceed it, but spatial mismatch prevents an energy-loss inference.
Float reconstruction uses existing Rec709 lum/quant_trunc, joined AC/Z/EB previews and logged ON jitter; 384-wide nearest previews approximate bilinear sampling.
Masks are frozen from both registered OFF repeats; rendered cross-window translations OFF-b -15 px / ON-b -10 px expose framing/animation confounding. No cross-window beauty warps.
User's “much more stable” judgment agrees with reduced bright-contour residuals, but does not replace failed/noisy gates.
Ultra qualification FAIL: Dialogue3 gates do not pass, and predicted.json requires static S8 quality too; S8 0.12 ms supplies cost only. No preset change prepared.
Deleted 188 analyzed raw files (916,144,549 bytes): 144 burst .raw, 32 presented PNGs and 12 float sidecars; exact paths/bytes in JSON. Metadata, other previews, summaries and logs retained.
Verification: complete analysis ran; constant/zero/finite chain fixtures pass. No shader requests, ABI or settings changed.
