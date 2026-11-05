# CPU receipts

ms/frame, except scene/bvh/matrix timers (ms/build) and alpha calls/frame. Do not sum nested rows. Submit includes the driver; capture includes scene recording.

| Window | CPU timer | Median | Range | Receipts |
|---|---|---:|---:|---:|
| ultra--static-a | capture total (includes scene recording and driver draw) | 17.7600 | 16.6310-18.5160 | 3 |
| ultra--static-a | capture driver draw (nested) | 0.4220 | 0.4210-0.4400 | 3 |
| ultra--static-a | capture pre-draw | 11.4670 | 10.9930-11.9450 | 3 |
| ultra--static-a | capture pass after (includes scene recording) | 4.4000 | 3.7840-4.6220 | 3 |
| ultra--static-a | draw bookkeeping | 0.6410 | 0.6220-0.6660 | 3 |
| ultra--static-a | hit material prepass (nested) | 1.9010 | 1.8570-1.9740 | 3 |
| ultra--static-a | scene bvh_build | 3.4380 | 3.4380-3.4380 | 1 |
| ultra--static-a | scene alpha_commit | 0.2840 | 0.2840-0.2840 | 1 |
| ultra--static-a | scene capture end to commit (includes bvh/alpha) | 3.9680 | 3.9680-3.9680 | 1 |
| ultra--static-a | bvh decide | 0.2325 | 0.2110-0.2540 | 2 |
| ultra--static-a | bvh per-draw BLAS keys | 1.1265 | 1.1240-1.1290 | 2 |
| ultra--static-a | bvh TLAS instances | 0.9300 | 0.8820-0.9780 | 2 |
| ultra--static-a | submit matrix copies (nested) | 0.6940 | 0.6940-0.6940 | 1 |
| ultra--static-a | hit material G-buffer hooks (nested) | 2.7770 | 2.6270-2.8560 | 3 |
| ultra--static-a | hit material query (nested) | 1.6800 | 1.6280-1.7140 | 3 |
| ultra--static-a | hit material key construction (nested) | 0.2140 | 0.2040-0.2200 | 3 |
| ultra--static-a | hit material mutex waits (nested) | 0.3420 | 0.3370-0.3450 | 3 |
| ultra--static-a | hit material collect (nested) | 0.4910 | 0.4720-0.4980 | 3 |
| ultra--static-a | capture hook bodies (driver draw excluded) | 17.3380 | 16.1910-18.0950 | 3 |
| ultra--static-a | submit inclusive (driver included) | 1.5397 | 1.4209-1.5399 | 3 |
| ultra--static-a | descriptor tracker | 0.0150 | 0.0024-0.0190 | 3 |
| ultra--static-a | alpha clone driver updates | 0.7832 | 0.7254-0.8425 | 3 |
| ultra--static-a | alpha clone calls/frame | 2.0000 | 2.0000-2.0000 | 3 |
| ultra--pan-q | capture total (includes scene recording and driver draw) | 23.0280 | 21.9740-24.0820 | 2 |
| ultra--pan-q | capture driver draw (nested) | 0.4865 | 0.4730-0.5000 | 2 |
| ultra--pan-q | capture pre-draw | 12.8195 | 12.2640-13.3750 | 2 |
| ultra--pan-q | capture pass after (includes scene recording) | 7.8270 | 7.7100-7.9440 | 2 |
| ultra--pan-q | draw bookkeeping | 0.6475 | 0.6430-0.6520 | 2 |
| ultra--pan-q | hit material prepass (nested) | 1.9800 | 1.9410-2.0190 | 2 |
| ultra--pan-q | bvh decide | 0.3550 | 0.3550-0.3550 | 1 |
| ultra--pan-q | bvh per-draw BLAS keys | 1.9470 | 1.9470-1.9470 | 1 |
| ultra--pan-q | bvh TLAS instances | 1.0300 | 1.0300-1.0300 | 1 |
| ultra--pan-q | hit material G-buffer hooks (nested) | 3.8380 | 3.7310-3.9450 | 2 |
| ultra--pan-q | hit material query (nested) | 1.7270 | 1.7140-1.7400 | 2 |
| ultra--pan-q | hit material key construction (nested) | 0.2795 | 0.2550-0.3040 | 2 |
| ultra--pan-q | hit material mutex waits (nested) | 0.3500 | 0.3390-0.3610 | 2 |
| ultra--pan-q | hit material collect (nested) | 0.5150 | 0.4930-0.5370 | 2 |
| ultra--pan-q | capture hook bodies (driver draw excluded) | 22.5415 | 21.5010-23.5820 | 2 |
| ultra--pan-q | submit inclusive (driver included) | 1.9625 | 1.6756-2.2494 | 2 |
| ultra--pan-q | descriptor tracker | 0.8165 | 0.5421-1.0909 | 2 |
| ultra--pan-q | alpha clone driver updates | 0.8382 | 0.8193-0.8571 | 2 |
| ultra--pan-q | alpha clone calls/frame | 2.0000 | 2.0000-2.0000 | 2 |
| high--static-a | capture total (includes scene recording and driver draw) | 8.1520 | 8.1520-8.1520 | 1 |
| high--static-a | capture driver draw (nested) | 0.2610 | 0.2610-0.2610 | 1 |
| high--static-a | capture pre-draw | 3.3550 | 3.3550-3.3550 | 1 |
| high--static-a | capture pass after (includes scene recording) | 3.6220 | 3.6220-3.6220 | 1 |
| high--static-a | draw bookkeeping | 0.5340 | 0.5340-0.5340 | 1 |
| high--static-a | hit material prepass (nested) | 0.3410 | 0.3410-0.3410 | 1 |
| high--static-a | hit material G-buffer hooks (nested) | 0.0010 | 0.0010-0.0010 | 1 |
| high--static-a | hit material query (nested) | 0.0000 | 0.0000-0.0000 | 1 |
| high--static-a | hit material key construction (nested) | 0.0400 | 0.0400-0.0400 | 1 |
| high--static-a | hit material mutex waits (nested) | 0.0030 | 0.0030-0.0030 | 1 |
| high--static-a | hit material collect (nested) | 0.0170 | 0.0170-0.0170 | 1 |
| high--static-a | capture hook bodies (driver draw excluded) | 7.8910 | 7.8910-7.8910 | 1 |
| high--static-a | submit inclusive (driver included) | 1.3004 | 1.3004-1.3004 | 1 |
| high--static-a | descriptor tracker | 0.0037 | 0.0037-0.0037 | 1 |
| high--static-a | alpha clone driver updates | 0.0000 | 0.0000-0.0000 | 1 |
| high--static-a | alpha clone calls/frame | 0.0000 | 0.0000-0.0000 | 1 |
| high--pan-q | capture total (includes scene recording and driver draw) | 9.7870 | 9.7870-9.7870 | 1 |
| high--pan-q | capture driver draw (nested) | 0.2960 | 0.2960-0.2960 | 1 |
| high--pan-q | capture pre-draw | 4.0790 | 4.0790-4.0790 | 1 |
| high--pan-q | capture pass after (includes scene recording) | 4.3850 | 4.3850-4.3850 | 1 |
| high--pan-q | draw bookkeeping | 0.6170 | 0.6170-0.6170 | 1 |
| high--pan-q | hit material prepass (nested) | 0.4720 | 0.4720-0.4720 | 1 |
| high--pan-q | hit material G-buffer hooks (nested) | 0.0010 | 0.0010-0.0010 | 1 |
| high--pan-q | hit material query (nested) | 0.0000 | 0.0000-0.0000 | 1 |
| high--pan-q | hit material key construction (nested) | 0.0480 | 0.0480-0.0480 | 1 |
| high--pan-q | hit material mutex waits (nested) | 0.0050 | 0.0050-0.0050 | 1 |
| high--pan-q | hit material collect (nested) | 0.0150 | 0.0150-0.0150 | 1 |
| high--pan-q | capture hook bodies (driver draw excluded) | 9.4910 | 9.4910-9.4910 | 1 |
| high--pan-q | submit inclusive (driver included) | 1.3505 | 1.3505-1.3505 | 1 |
| high--pan-q | descriptor tracker | 0.6395 | 0.6395-0.6395 | 1 |
| high--pan-q | alpha clone driver updates | 0.0000 | 0.0000-0.0000 | 1 |
| high--pan-q | alpha clone calls/frame | 0.0000 | 0.0000-0.0000 | 1 |
