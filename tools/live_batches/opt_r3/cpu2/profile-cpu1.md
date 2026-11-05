# CPU receipts

ms/frame, except scene/bvh/matrix timers (ms/build) and alpha calls/frame. Do not sum nested rows. Submit includes the driver; capture includes scene recording.

| Window | CPU timer | Median | Range | Receipts |
|---|---|---:|---:|---:|
| off-a--static-a | capture total (includes scene recording and driver draw) | 12.6860 | 12.5030-12.9030 | 4 |
| off-a--static-a | capture driver draw (nested) | 0.2930 | 0.2840-0.2970 | 4 |
| off-a--static-a | capture pre-draw | 8.6960 | 8.5950-8.7700 | 4 |
| off-a--static-a | capture pass after (includes scene recording) | 2.5500 | 2.4860-2.7240 | 4 |
| off-a--static-a | draw bookkeeping | 0.5250 | 0.5210-0.5320 | 4 |
| off-a--static-a | hit material prepass (nested) | 1.5690 | 1.5660-1.5920 | 4 |
| off-a--static-a | scene bvh_build | 1.9870 | 1.9870-1.9870 | 1 |
| off-a--static-a | scene alpha_commit | 0.1750 | 0.1750-0.1750 | 1 |
| off-a--static-a | scene capture end to commit (includes bvh/alpha) | 2.3210 | 2.3210-2.3210 | 1 |
| off-a--static-a | bvh decide | 0.1280 | 0.1260-0.1320 | 4 |
| off-a--static-a | bvh per-draw BLAS keys | 0.4900 | 0.4750-0.5010 | 4 |
| off-a--static-a | bvh TLAS instances | 0.6380 | 0.6210-0.6540 | 4 |
| off-a--static-a | submit matrix copies (nested) | 0.5550 | 0.5550-0.5550 | 1 |
| off-a--static-a | hit material G-buffer hooks (nested) | 2.0635 | 2.0350-2.0880 | 4 |
| off-a--static-a | hit material query (nested) | 0.9440 | 0.9320-0.9730 | 4 |
| off-a--static-a | hit material key construction (nested) | 0.1635 | 0.1630-0.1650 | 4 |
| off-a--static-a | hit material mutex waits (nested) | 0.2765 | 0.2730-0.2820 | 4 |
| off-a--static-a | hit material collect (nested) | 0.3880 | 0.3850-0.3970 | 4 |
| off-a--static-a | capture hook bodies (driver draw excluded) | 12.3915 | 12.2190-12.6090 | 4 |
| off-a--static-a | submit inclusive (driver included) | 1.0644 | 1.0525-1.0813 | 5 |
| off-a--static-a | descriptor tracker | 0.0028 | 0.0015-0.0158 | 5 |
| off-a--static-a | alpha clone driver updates | 0.5119 | 0.5032-0.5249 | 5 |
| off-a--static-a | alpha clone calls/frame | 2.0000 | 2.0000-2.0000 | 5 |
| on-a--static-a | capture total (includes scene recording and driver draw) | 12.5475 | 12.3010-12.9030 | 4 |
| on-a--static-a | capture driver draw (nested) | 0.2940 | 0.2790-0.3090 | 4 |
| on-a--static-a | capture pre-draw | 8.7010 | 8.5370-8.9480 | 4 |
| on-a--static-a | capture pass after (includes scene recording) | 2.4110 | 2.3650-2.4860 | 4 |
| on-a--static-a | draw bookkeeping | 0.5265 | 0.5220-0.5400 | 4 |
| on-a--static-a | hit material prepass (nested) | 1.5755 | 1.5590-1.5970 | 4 |
| on-a--static-a | scene bvh_build | 1.8580 | 1.8580-1.8580 | 1 |
| on-a--static-a | scene alpha_commit | 0.1710 | 0.1710-0.1710 | 1 |
| on-a--static-a | scene capture end to commit (includes bvh/alpha) | 2.1880 | 2.1880-2.1880 | 1 |
| on-a--static-a | bvh decide | 0.0670 | 0.0660-0.0690 | 4 |
| on-a--static-a | bvh per-draw BLAS keys | 0.4045 | 0.3960-0.4100 | 4 |
| on-a--static-a | bvh TLAS instances | 0.6495 | 0.6320-0.6620 | 4 |
| on-a--static-a | submit matrix copies (nested) | 0.5480 | 0.5480-0.5480 | 1 |
| on-a--static-a | hit material G-buffer hooks (nested) | 2.0380 | 1.9940-2.0900 | 4 |
| on-a--static-a | hit material query (nested) | 0.9415 | 0.9290-0.9570 | 4 |
| on-a--static-a | hit material key construction (nested) | 0.1640 | 0.1590-0.1710 | 4 |
| on-a--static-a | hit material mutex waits (nested) | 0.2715 | 0.2480-0.2800 | 4 |
| on-a--static-a | hit material collect (nested) | 0.3860 | 0.3780-0.3920 | 4 |
| on-a--static-a | capture hook bodies (driver draw excluded) | 12.2535 | 12.0220-12.5940 | 4 |
| on-a--static-a | submit inclusive (driver included) | 1.0453 | 1.0369-1.1034 | 5 |
| on-a--static-a | descriptor tracker | 0.0060 | 0.0020-0.0080 | 5 |
| on-a--static-a | alpha clone driver updates | 0.5102 | 0.5075-0.5167 | 5 |
| on-a--static-a | alpha clone calls/frame | 2.0000 | 2.0000-2.0000 | 5 |
| off-b--static-a | capture total (includes scene recording and driver draw) | 12.6650 | 12.5860-13.4730 | 5 |
| off-b--static-a | capture driver draw (nested) | 0.2910 | 0.2870-0.3120 | 5 |
| off-b--static-a | capture pre-draw | 8.6240 | 8.6200-9.0660 | 5 |
| off-b--static-a | capture pass after (includes scene recording) | 2.6360 | 2.5520-2.9210 | 5 |
| off-b--static-a | draw bookkeeping | 0.5260 | 0.5170-0.5460 | 5 |
| off-b--static-a | hit material prepass (nested) | 1.5690 | 1.5390-1.6120 | 5 |
| off-b--static-a | scene bvh_build | 2.1190 | 2.1190-2.1190 | 1 |
| off-b--static-a | scene alpha_commit | 0.1720 | 0.1720-0.1720 | 1 |
| off-b--static-a | scene capture end to commit (includes bvh/alpha) | 2.4500 | 2.4500-2.4500 | 1 |
| off-b--static-a | bvh decide | 0.1315 | 0.1260-0.1400 | 4 |
| off-b--static-a | bvh per-draw BLAS keys | 0.5280 | 0.5050-0.5570 | 4 |
| off-b--static-a | bvh TLAS instances | 0.7040 | 0.6540-0.8140 | 4 |
| off-b--static-a | submit matrix copies (nested) | 0.5500 | 0.5500-0.5500 | 1 |
| off-b--static-a | hit material G-buffer hooks (nested) | 2.0320 | 2.0240-2.1220 | 5 |
| off-b--static-a | hit material query (nested) | 0.9380 | 0.9250-0.9890 | 5 |
| off-b--static-a | hit material key construction (nested) | 0.1650 | 0.1600-0.1750 | 5 |
| off-b--static-a | hit material mutex waits (nested) | 0.2700 | 0.2380-0.2800 | 5 |
| off-b--static-a | hit material collect (nested) | 0.3830 | 0.3760-0.3960 | 5 |
| off-b--static-a | capture hook bodies (driver draw excluded) | 12.3760 | 12.2950-13.1610 | 5 |
| off-b--static-a | submit inclusive (driver included) | 1.0594 | 1.0307-1.1370 | 5 |
| off-b--static-a | descriptor tracker | 0.0030 | 0.0018-0.0152 | 5 |
| off-b--static-a | alpha clone driver updates | 0.5133 | 0.5034-0.5301 | 5 |
| off-b--static-a | alpha clone calls/frame | 2.0000 | 2.0000-2.0000 | 5 |
| on-b--static-a | capture total (includes scene recording and driver draw) | 12.6420 | 12.2750-13.2310 | 5 |
| on-b--static-a | capture driver draw (nested) | 0.2920 | 0.2790-0.3190 | 5 |
| on-b--static-a | capture pre-draw | 8.7550 | 8.5300-9.1940 | 5 |
| on-b--static-a | capture pass after (includes scene recording) | 2.4450 | 2.3540-2.5180 | 5 |
| on-b--static-a | draw bookkeeping | 0.5230 | 0.5210-0.5480 | 5 |
| on-b--static-a | hit material prepass (nested) | 1.5780 | 1.5560-1.6520 | 5 |
| on-b--static-a | scene bvh_build | 1.8805 | 1.8650-1.8960 | 2 |
| on-b--static-a | scene alpha_commit | 0.1775 | 0.1740-0.1810 | 2 |
| on-b--static-a | scene capture end to commit (includes bvh/alpha) | 2.2225 | 2.2000-2.2450 | 2 |
| on-b--static-a | bvh decide | 0.0710 | 0.0680-0.0770 | 4 |
| on-b--static-a | bvh per-draw BLAS keys | 0.4330 | 0.4060-0.4410 | 4 |
| on-b--static-a | bvh TLAS instances | 0.6480 | 0.6250-0.6520 | 4 |
| on-b--static-a | submit matrix copies (nested) | 0.5685 | 0.5560-0.5810 | 2 |
| on-b--static-a | hit material G-buffer hooks (nested) | 2.0580 | 2.0240-2.1470 | 5 |
| on-b--static-a | hit material query (nested) | 0.9420 | 0.9260-1.0160 | 5 |
| on-b--static-a | hit material key construction (nested) | 0.1620 | 0.1610-0.1720 | 5 |
| on-b--static-a | hit material mutex waits (nested) | 0.2700 | 0.2580-0.2860 | 5 |
| on-b--static-a | hit material collect (nested) | 0.3900 | 0.3820-0.4000 | 5 |
| on-b--static-a | capture hook bodies (driver draw excluded) | 12.3500 | 11.9960-12.9120 | 5 |
| on-b--static-a | submit inclusive (driver included) | 1.0908 | 1.0391-1.1824 | 5 |
| on-b--static-a | descriptor tracker | 0.0028 | 0.0019-0.0074 | 5 |
| on-b--static-a | alpha clone driver updates | 0.5148 | 0.5060-0.5550 | 5 |
| on-b--static-a | alpha clone calls/frame | 2.0000 | 2.0000-2.0000 | 5 |

Signed ON deltas against the median of both OFF medians; negative saves CPU.

| Timer | OFF repeat spread | ON a delta | ON b delta |
|---|---:|---:|---:|
| capture total (includes scene recording and driver draw) | 0.0210 | -0.1280 | -0.0335 |
| capture driver draw (nested) | 0.0020 | +0.0020 | +0.0000 |
| capture pre-draw | 0.0720 | +0.0410 | +0.0950 |
| capture pass after (includes scene recording) | 0.0860 | -0.1820 | -0.1480 |
| draw bookkeeping | 0.0010 | +0.0010 | -0.0025 |
| hit material prepass (nested) | 0.0000 | +0.0065 | +0.0090 |
| scene bvh_build | 0.1320 | -0.1950 | -0.1725 |
| scene alpha_commit | 0.0030 | -0.0025 | +0.0040 |
| scene capture end to commit (includes bvh/alpha) | 0.1290 | -0.1975 | -0.1630 |
| bvh decide | 0.0035 | -0.0628 | -0.0587 |
| bvh per-draw BLAS keys | 0.0380 | -0.1045 | -0.0760 |
| bvh TLAS instances | 0.0660 | -0.0215 | -0.0230 |
| submit matrix copies (nested) | 0.0050 | -0.0045 | +0.0160 |
| hit material G-buffer hooks (nested) | 0.0315 | -0.0097 | +0.0102 |
| hit material query (nested) | 0.0060 | +0.0005 | +0.0010 |
| hit material key construction (nested) | 0.0015 | -0.0003 | -0.0023 |
| hit material mutex waits (nested) | 0.0065 | -0.0017 | -0.0032 |
| hit material collect (nested) | 0.0050 | +0.0005 | +0.0045 |
| capture hook bodies (driver draw excluded) | 0.0155 | -0.1303 | -0.0337 |
| submit inclusive (driver included) | 0.0051 | -0.0166 | +0.0289 |
| descriptor tracker | 0.0002 | +0.0031 | -0.0001 |
| alpha clone driver updates | 0.0014 | -0.0024 | +0.0022 |
| alpha clone calls/frame | 0.0000 | +0.0000 | +0.0000 |
