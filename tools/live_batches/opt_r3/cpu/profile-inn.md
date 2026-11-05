# CPU receipts

ms/frame, except scene/bvh/matrix timers (ms/build) and alpha calls/frame. Do not sum nested rows. Submit includes the driver; capture includes scene recording.

| Window | CPU timer | Median | Range | Receipts |
|---|---|---:|---:|---:|
| ultra--static-a | capture total (includes scene recording and driver draw) | 12.6225 | 12.5710-12.6740 | 2 |
| ultra--static-a | capture driver draw (nested) | 0.2820 | 0.2820-0.2820 | 2 |
| ultra--static-a | capture pre-draw | 8.7850 | 8.7680-8.8020 | 2 |
| ultra--static-a | capture pass after (includes scene recording) | 2.4490 | 2.4120-2.4860 | 2 |
| ultra--static-a | draw bookkeeping | 0.5120 | 0.5110-0.5130 | 2 |
| ultra--static-a | hit material prepass (nested) | 1.5490 | 1.5440-1.5540 | 2 |
| ultra--static-a | bvh decide | 0.1210 | 0.1210-0.1210 | 2 |
| ultra--static-a | bvh per-draw BLAS keys | 0.4965 | 0.4850-0.5080 | 2 |
| ultra--static-a | bvh TLAS instances | 0.6375 | 0.6290-0.6460 | 2 |
| ultra--static-a | capture hook bodies (driver draw excluded) | 12.3405 | 12.2890-12.3920 | 2 |
| ultra--static-a | submit inclusive (driver included) | 1.0373 | 1.0117-1.0713 | 3 |
| ultra--static-a | descriptor tracker | 0.0024 | 0.0023-0.0216 | 3 |
| ultra--static-a | alpha clone driver updates | 0.5062 | 0.5056-0.5080 | 3 |
| ultra--static-a | alpha clone calls/frame | 2.0000 | 2.0000-2.0000 | 3 |
| ultra--pan-q | capture total (includes scene recording and driver draw) | 15.3950 | 14.2850-15.6190 | 3 |
| ultra--pan-q | capture driver draw (nested) | 0.3220 | 0.3180-0.3400 | 3 |
| ultra--pan-q | capture pre-draw | 9.7640 | 9.1560-10.0680 | 3 |
| ultra--pan-q | capture pass after (includes scene recording) | 4.0470 | 3.7040-4.1140 | 3 |
| ultra--pan-q | draw bookkeeping | 0.4930 | 0.4490-0.5360 | 3 |
| ultra--pan-q | hit material prepass (nested) | 1.5510 | 1.3990-1.5930 | 3 |
| ultra--pan-q | scene bvh_build | 3.3250 | 3.3250-3.3250 | 1 |
| ultra--pan-q | scene alpha_commit | 0.1810 | 0.1810-0.1810 | 1 |
| ultra--pan-q | scene capture end to commit (includes bvh/alpha) | 3.6860 | 3.6860-3.6860 | 1 |
| ultra--pan-q | bvh decide | 0.2310 | 0.2280-0.2340 | 2 |
| ultra--pan-q | bvh per-draw BLAS keys | 0.7090 | 0.6560-0.7620 | 2 |
| ultra--pan-q | bvh TLAS instances | 0.8435 | 0.8370-0.8500 | 2 |
| ultra--pan-q | submit matrix copies (nested) | 0.6920 | 0.6920-0.6920 | 1 |
| ultra--pan-q | capture hook bodies (driver draw excluded) | 15.0550 | 13.9670-15.2970 | 3 |
| ultra--pan-q | submit inclusive (driver included) | 1.4529 | 1.4015-1.5976 | 3 |
| ultra--pan-q | descriptor tracker | 0.2810 | 0.2696-0.4792 | 3 |
| ultra--pan-q | alpha clone driver updates | 0.5183 | 0.5162-0.5531 | 3 |
| ultra--pan-q | alpha clone calls/frame | 2.0000 | 2.0000-2.0000 | 3 |
| high--static-a | capture total (includes scene recording and driver draw) | 7.1600 | 7.0540-7.4590 | 3 |
| high--static-a | capture driver draw (nested) | 0.1940 | 0.1920-0.1980 | 3 |
| high--static-a | capture pre-draw | 3.5770 | 3.5420-3.6100 | 3 |
| high--static-a | capture pass after (includes scene recording) | 2.5820 | 2.5150-2.8350 | 3 |
| high--static-a | draw bookkeeping | 0.5850 | 0.5830-0.5870 | 3 |
| high--static-a | hit material prepass (nested) | 0.1960 | 0.1930-0.2000 | 3 |
| high--static-a | bvh decide | 0.1550 | 0.1550-0.1550 | 2 |
| high--static-a | bvh per-draw BLAS keys | 0.5955 | 0.5830-0.6080 | 2 |
| high--static-a | bvh TLAS instances | 0.5365 | 0.5360-0.5370 | 2 |
| high--static-a | capture hook bodies (driver draw excluded) | 6.9660 | 6.8620-7.2610 | 3 |
| high--static-a | submit inclusive (driver included) | 0.9948 | 0.9905-0.9970 | 3 |
| high--static-a | descriptor tracker | 0.0075 | 0.0014-0.0178 | 3 |
| high--static-a | alpha clone driver updates | 0.0000 | 0.0000-0.0000 | 3 |
| high--static-a | alpha clone calls/frame | 0.0000 | 0.0000-0.0000 | 3 |
| high--pan-q | capture total (includes scene recording and driver draw) | 6.5390 | 6.4890-7.7460 | 3 |
| high--pan-q | capture driver draw (nested) | 0.1820 | 0.1810-0.1870 | 3 |
| high--pan-q | capture pre-draw | 3.2320 | 3.2150-3.8490 | 3 |
| high--pan-q | capture pass after (includes scene recording) | 2.4550 | 2.3760-2.9330 | 3 |
| high--pan-q | draw bookkeeping | 0.4840 | 0.4510-0.5750 | 3 |
| high--pan-q | hit material prepass (nested) | 0.2110 | 0.2090-0.2110 | 3 |
| high--pan-q | scene bvh_build | 2.3660 | 2.3660-2.3660 | 1 |
| high--pan-q | scene alpha_commit | 0.1860 | 0.1860-0.1860 | 1 |
| high--pan-q | scene capture end to commit (includes bvh/alpha) | 2.5710 | 2.5710-2.5710 | 1 |
| high--pan-q | bvh decide | 0.1920 | 0.1600-0.2240 | 2 |
| high--pan-q | bvh per-draw BLAS keys | 0.7785 | 0.6630-0.8940 | 2 |
| high--pan-q | bvh TLAS instances | 0.4310 | 0.3830-0.4790 | 2 |
| high--pan-q | submit matrix copies (nested) | 0.6280 | 0.6280-0.6280 | 1 |
| high--pan-q | capture hook bodies (driver draw excluded) | 6.3570 | 6.3020-7.5650 | 3 |
| high--pan-q | submit inclusive (driver included) | 0.9780 | 0.7614-1.0171 | 3 |
| high--pan-q | descriptor tracker | 0.3168 | 0.1816-0.5112 | 3 |
| high--pan-q | alpha clone driver updates | 0.0000 | 0.0000-0.0000 | 3 |
| high--pan-q | alpha clone calls/frame | 0.0000 | 0.0000-0.0000 | 3 |
