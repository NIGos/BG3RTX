# FPS window regeneration

Offline only on tools-fps-windows. No game, queue submission, commits or Git metadata writes.

102 readable VALID logs, 9 saves; lower nearest-rank p10 by save and diagnostic state.
Replay: 108 jobs checked, 106 have unsatisfied historical evidence; 2 have all requested historical log evidence.
Full gate failures: `captures/offline/fps_windows_replay.json` (generated; not a rendering pass).

| Job | Plan seconds before | After |
| --- | ---: | ---: |
| combined-Campsite---27h | 498.3 | 913.6 |
| combined-Test-Dialogue3 | 287.2 | 291.9 |
| combined-Water-Queens-House | 1538.6 | 1609.2 |
| cp4-compact-camp-join | 75.5 | 102.9 |
| hc1-dialogue1-fg2 | 232.0 | 398.5 |
| hc2-campsite-fg2 | 232.0 | 333.1 |
| hv1-dialogue1 | 247.7 | 343.7 |
| hv2-camp | 260.7 | 324.2 |
| hv3-s8 | 248.7 | 251.6 |
| hv4-gpuav | 84.3 | 398.8 |
| hv5-reload-bisect | 337.1 | 488.3 |
| pt-dialogue3 | 338.9 | 351.7 |
| pt-s8 | 130.3 | 131.5 |
| su1-ultra-s8-fg2 | 156.7 | 164.0 |
| wsf1-s8-interface-column | 139.0 | 126.7 |

Remaining preflight blockers (gates and limits retained):

- cp4: 102.9 s plan + 800.868 s readiness exceeds the 900 s cap by 3.768 s.
- Combined camp: 913.6 s plan + 1384.75 s readiness exceeds 900 s.
- Combined S8: 1609.2 s plan + 420.456 s readiness exceeds 900 s. Split/replan these drafts before queuing.
- Hair wrapper: 71.1 min aggregate estimate exceeds its 65 min allowance. All 5 per-job duration checks and both 5-job fake-queue paths pass.
- Same-save crowd plans now have separate files: Dialogue1 needs 398.5 s, camp 333.1 s; a shared plan previously let the later save shorten the first.

Every changed window (seconds; every expect/expect_every/expect_runner/forbid value and count unchanged):

| Job | Window | Before | After |
| --- | --- | ---: | ---: |
| combined-Campsite---27h | b1-cp4-compact-camp-join--observe-on | 9.1 | 32.8 |
| combined-Campsite---27h | b1-cp4-compact-camp-join--observe-on-repeat | 9.1 | 32.8 |
| combined-Campsite---27h | b2-go1-camp--shot-off-a | 10.1 | 21.4 |
| combined-Campsite---27h | b2-go1-camp--cost-off-a | 21.1 | 49.5 |
| combined-Campsite---27h | b2-go1-camp--shot-compact | 10.1 | 21.4 |
| combined-Campsite---27h | b2-go1-camp--cost-compact | 21.1 | 49.5 |
| combined-Campsite---27h | b2-go1-camp--shot-off-b | 10.1 | 21.4 |
| combined-Campsite---27h | b2-go1-camp--cost-off-b | 21.1 | 49.5 |
| combined-Campsite---27h | b2-go1-camp--shot-alpha | 10.1 | 21.4 |
| combined-Campsite---27h | b2-go1-camp--cost-alpha | 21.1 | 49.5 |
| combined-Campsite---27h | b2-go1-camp--shot-off-c | 10.1 | 21.4 |
| combined-Campsite---27h | b2-go1-camp--cost-off-c | 21.1 | 49.5 |
| combined-Campsite---27h | b2-go1-camp--shot-water | 10.1 | 21.4 |
| combined-Campsite---27h | b2-go1-camp--cost-water | 21.1 | 49.5 |
| combined-Campsite---27h | b2-go1-camp--shot-off-d | 10.1 | 21.4 |
| combined-Campsite---27h | b2-go1-camp--cost-off-d | 21.1 | 49.5 |
| combined-Campsite---27h | b3-go2-camp-high--settle-off-a | 2.1 | 3.6 |
| combined-Campsite---27h | b3-go2-camp-high--cost-off-a | 21.1 | 49.5 |
| combined-Campsite---27h | b3-go2-camp-high--shot-off-a | 15.1 | 15.2 |
| combined-Campsite---27h | b3-go2-camp-high--settle-on | 2.1 | 3.6 |
| combined-Campsite---27h | b3-go2-camp-high--cost-on | 21.1 | 49.5 |
| combined-Campsite---27h | b3-go2-camp-high--shot-on | 15.1 | 15.2 |
| combined-Campsite---27h | b3-go2-camp-high--settle-off-b | 2.1 | 3.6 |
| combined-Campsite---27h | b3-go2-camp-high--cost-off-b | 21.1 | 49.5 |
| combined-Campsite---27h | b3-go2-camp-high--shot-off-b | 15.1 | 15.2 |
| combined-Test-Dialogue3 | b1-pt-dialogue3--settle-off-a | 2.1 | 2.3 |
| combined-Test-Dialogue3 | b1-pt-dialogue3--presented-off-a | 3.1 | 3.2 |
| combined-Test-Dialogue3 | b1-pt-dialogue3--settle-on-a | 4.7 | 5.3 |
| combined-Test-Dialogue3 | b1-pt-dialogue3--presented-on-a | 3.1 | 3.2 |
| combined-Test-Dialogue3 | b1-pt-dialogue3--settle-off-b | 2.1 | 2.3 |
| combined-Test-Dialogue3 | b1-pt-dialogue3--presented-off-b | 3.1 | 3.2 |
| combined-Test-Dialogue3 | b1-pt-dialogue3--settle-on-b | 4.7 | 5.3 |
| combined-Test-Dialogue3 | b1-pt-dialogue3--presented-on-b | 3.1 | 3.2 |
| combined-Test-Dialogue3 | b2-ef1-dialogue3-attribution--settle-base-a | 2.1 | 2.3 |
| combined-Test-Dialogue3 | b2-ef1-dialogue3-attribution--presented-base-a | 3.6 | 3.7 |
| combined-Test-Dialogue3 | b2-ef1-dialogue3-attribution--settle-fg-off | 2.1 | 2.3 |
| combined-Test-Dialogue3 | b2-ef1-dialogue3-attribution--presented-fg-off | 3.6 | 3.7 |
| combined-Test-Dialogue3 | b2-ef1-dialogue3-attribution--settle-ao-off | 2.1 | 2.3 |
| combined-Test-Dialogue3 | b2-ef1-dialogue3-attribution--presented-ao-off | 3.6 | 3.7 |
| combined-Test-Dialogue3 | b2-ef1-dialogue3-attribution--settle-sun-off | 2.1 | 2.3 |
| combined-Test-Dialogue3 | b2-ef1-dialogue3-attribution--presented-sun-off | 3.6 | 3.7 |
| combined-Test-Dialogue3 | b2-ef1-dialogue3-attribution--settle-gi-off | 2.1 | 2.3 |
| combined-Test-Dialogue3 | b2-ef1-dialogue3-attribution--presented-gi-off | 3.6 | 3.7 |
| combined-Test-Dialogue3 | b2-ef1-dialogue3-attribution--settle-rr-off | 2.1 | 2.3 |
| combined-Test-Dialogue3 | b2-ef1-dialogue3-attribution--presented-rr-off | 3.6 | 3.7 |
| combined-Test-Dialogue3 | b2-ef1-dialogue3-attribution--settle-cbt-off | 2.1 | 2.3 |
| combined-Test-Dialogue3 | b2-ef1-dialogue3-attribution--presented-cbt-off | 3.6 | 3.7 |
| combined-Test-Dialogue3 | b2-ef1-dialogue3-attribution--settle-post-late | 2.1 | 2.3 |
| combined-Test-Dialogue3 | b2-ef1-dialogue3-attribution--presented-post-late | 3.6 | 3.7 |
| combined-Test-Dialogue3 | b2-ef1-dialogue3-attribution--settle-base-b | 2.1 | 2.3 |
| combined-Test-Dialogue3 | b2-ef1-dialogue3-attribution--presented-base-b | 3.6 | 3.7 |
| combined-Water-Queens-House | b1-su1-ultra-s8-fg2--diag-omm | 9.1 | 16.4 |
| combined-Water-Queens-House | b2-go2-s8--shot-off-a | 10.1 | 10.9 |
| combined-Water-Queens-House | b2-go2-s8--cost-off-a | 21.1 | 23.2 |
| combined-Water-Queens-House | b2-go2-s8--shot-compact | 10.1 | 10.9 |
| combined-Water-Queens-House | b2-go2-s8--cost-compact | 21.1 | 23.2 |
| combined-Water-Queens-House | b2-go2-s8--shot-off-b | 10.1 | 10.9 |
| combined-Water-Queens-House | b2-go2-s8--cost-off-b | 21.1 | 23.2 |
| combined-Water-Queens-House | b2-go2-s8--shot-alpha | 10.1 | 10.9 |
| combined-Water-Queens-House | b2-go2-s8--cost-alpha | 21.1 | 23.2 |
| combined-Water-Queens-House | b2-go2-s8--shot-off-c | 10.1 | 10.9 |
| combined-Water-Queens-House | b2-go2-s8--cost-off-c | 21.1 | 23.2 |
| combined-Water-Queens-House | b2-go2-s8--shot-water | 10.1 | 10.9 |
| combined-Water-Queens-House | b2-go2-s8--cost-water | 21.1 | 23.2 |
| combined-Water-Queens-House | b2-go2-s8--shot-off-d | 10.1 | 10.9 |
| combined-Water-Queens-House | b2-go2-s8--cost-off-d | 21.1 | 23.2 |
| combined-Water-Queens-House | b3-go2-s8-medium--settle-off-a | 2.1 | 2.2 |
| combined-Water-Queens-House | b3-go2-s8-medium--cost-off-a | 21.1 | 23.2 |
| combined-Water-Queens-House | b3-go2-s8-medium--settle-on | 2.1 | 2.2 |
| combined-Water-Queens-House | b3-go2-s8-medium--cost-on | 21.1 | 23.2 |
| combined-Water-Queens-House | b3-go2-s8-medium--settle-off-b | 2.1 | 2.2 |
| combined-Water-Queens-House | b3-go2-s8-medium--cost-off-b | 21.1 | 23.2 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--shot-sparse-off-a | 17.1 | 17.9 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--shot-sparse-on-a | 17.1 | 17.9 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--shot-sparse-off-b | 17.1 | 17.9 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--shot-sparse-on-b | 17.1 | 17.9 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--cost-sparse-0 | 13.1 | 14.3 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--cost-sparse-1 | 13.1 | 14.3 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--cost-sparse-2 | 13.1 | 14.3 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--cost-sparse-3 | 13.1 | 14.3 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--shot-cone-off-a | 17.1 | 17.9 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--shot-cone-on-a | 17.1 | 17.9 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--shot-cone-off-b | 17.1 | 17.9 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--shot-cone-on-b | 17.1 | 17.9 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--cost-cone-0 | 13.1 | 14.3 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--cost-cone-1 | 13.1 | 14.3 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--cost-cone-2 | 13.1 | 14.3 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--cost-cone-3 | 13.1 | 14.3 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--shot-order-off-a | 17.1 | 17.9 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--shot-order-on-a | 17.1 | 17.9 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--shot-order-off-b | 17.1 | 17.9 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--shot-order-on-b | 17.1 | 17.9 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--cost-order-0 | 13.1 | 14.3 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--cost-order-1 | 13.1 | 14.3 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--cost-order-2 | 13.1 | 14.3 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--cost-order-3 | 13.1 | 14.3 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--shot-combined-off-a | 17.1 | 17.9 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--shot-combined-on-a | 17.1 | 17.9 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--shot-combined-off-b | 17.1 | 17.9 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--shot-combined-on-b | 17.1 | 17.9 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--cost-combined-0 | 13.1 | 14.3 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--cost-combined-1 | 13.1 | 14.3 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--cost-combined-2 | 13.1 | 14.3 |
| combined-Water-Queens-House | b4-wo1-s8-hit-shade--cost-combined-3 | 13.1 | 14.3 |
| combined-Water-Queens-House | b5-wf9-s8-compose-shift--energy-static-0 | 10.1 | 10.9 |
| combined-Water-Queens-House | b5-wf9-s8-compose-shift--energy-static-1 | 10.1 | 10.9 |
| combined-Water-Queens-House | b5-wf9-s8-compose-shift--energy-static-2 | 10.1 | 10.9 |
| combined-Water-Queens-House | b5-wf9-s8-compose-shift--energy-static-3 | 10.1 | 10.9 |
| combined-Water-Queens-House | b6-pt-s8--settle-off-a | 2.1 | 2.2 |
| combined-Water-Queens-House | b6-pt-s8--presented-off-a | 3.1 | 3.2 |
| combined-Water-Queens-House | b6-pt-s8--settle-on-a | 4.7 | 5 |
| combined-Water-Queens-House | b6-pt-s8--presented-on-a | 3.1 | 3.2 |
| combined-Water-Queens-House | b6-pt-s8--settle-off-b | 2.1 | 2.2 |
| combined-Water-Queens-House | b6-pt-s8--presented-off-b | 3.1 | 3.2 |
| combined-Water-Queens-House | b6-pt-s8--settle-on-b | 4.7 | 5 |
| combined-Water-Queens-House | b6-pt-s8--presented-on-b | 3.1 | 3.2 |
| cp4-compact-camp-join | observe-on | 19.1 | 32.8 |
| cp4-compact-camp-join | observe-on-repeat | 19.1 | 32.8 |
| hc1-dialogue1-fg2 | settle-cam | 10 | 4.3 |
| hc1-dialogue1-fg2 | shot-cards | 8 | 14.4 |
| hc1-dialogue1-fg2 | shot-strands | 8 | 14.4 |
| hc1-dialogue1-fg2 | shot-crowd | 8 | 14.4 |
| hc1-dialogue1-fg2 | shot-cards-b | 8 | 14.4 |
| hc1-dialogue1-fg2 | settle | 5 | 4.3 |
| hc1-dialogue1-fg2 | cost-cards | 25 | 62.8 |
| hc1-dialogue1-fg2 | cost-strands | 25 | 62.8 |
| hc1-dialogue1-fg2 | cost-crowd | 25 | 62.8 |
| hc1-dialogue1-fg2 | cost-cards-b | 25 | 62.8 |
| hc1-dialogue1-fg2 | restore | 5 | 1.1 |
| hc2-campsite-fg2 | settle-cam | 10 | 3.6 |
| hc2-campsite-fg2 | shot-cards | 8 | 11.7 |
| hc2-campsite-fg2 | shot-strands | 8 | 11.7 |
| hc2-campsite-fg2 | shot-crowd | 8 | 11.7 |
| hc2-campsite-fg2 | shot-cards-b | 8 | 11.7 |
| hc2-campsite-fg2 | settle | 5 | 3.6 |
| hc2-campsite-fg2 | cost-cards | 25 | 49.5 |
| hc2-campsite-fg2 | cost-strands | 25 | 49.5 |
| hc2-campsite-fg2 | cost-crowd | 25 | 49.5 |
| hc2-campsite-fg2 | cost-cards-b | 25 | 49.5 |
| hc2-campsite-fg2 | restore | 5 | 1.1 |
| hv1-dialogue1 | shot-cards-a | 6.1 | 6.3 |
| hv1-dialogue1 | shot-strands-a | 6.1 | 6.3 |
| hv1-dialogue1 | shot-cards-b | 6.1 | 6.3 |
| hv1-dialogue1 | shot-strands-b | 6.1 | 6.3 |
| hv1-dialogue1 | shot-cards-c | 6.1 | 6.3 |
| hv1-dialogue1 | cost-cards-a | 5.1 | 13.4 |
| hv1-dialogue1 | cost-strands | 21.1 | 62.8 |
| hv1-dialogue1 | cost-cards-b | 5.1 | 13.4 |
| hv1-dialogue1 | cost-after-reload | 26.1 | 62.8 |
| hv2-camp | shot-cards-a | 6.1 | 6.2 |
| hv2-camp | shot-strands-a | 6.1 | 6.2 |
| hv2-camp | shot-cards-b | 6.1 | 6.2 |
| hv2-camp | shot-strands-b | 6.1 | 6.2 |
| hv2-camp | shot-cards-c | 6.1 | 6.2 |
| hv2-camp | cost-cards-a | 5.1 | 10.7 |
| hv2-camp | cost-strands | 21.1 | 49.5 |
| hv2-camp | cost-cards-b | 5.1 | 10.7 |
| hv2-camp | cost-after-reload | 26.1 | 49.5 |
| hv3-s8 | cost-cards-a | 5.1 | 5.5 |
| hv3-s8 | cost-strands | 21.1 | 23.2 |
| hv3-s8 | cost-cards-b | 5.1 | 5.5 |
| hv4-gpuav | validation-strands | 25 | 182.8 |
| hv4-gpuav | validation-after-reload | 26.1 | 182.8 |
| hv5-reload-bisect | survive-c-strands-2 | 25 | 62.8 |
| hv5-reload-bisect | survive-c-strands-3 | 25 | 62.8 |
| hv5-reload-bisect | survive-d-fibre-2 | 25 | 62.8 |
| hv5-reload-bisect | survive-d-fibre-3 | 25 | 62.8 |
| pt-dialogue3 | settle-off-00 | 2.1 | 2.3 |
| pt-dialogue3 | presented-off-00 | 3.1 | 3.2 |
| pt-dialogue3 | settle-on-00 | 4.7 | 5.3 |
| pt-dialogue3 | presented-on-00 | 3.1 | 3.2 |
| pt-dialogue3 | settle-on-01 | 4.7 | 5.3 |
| pt-dialogue3 | presented-on-01 | 3.1 | 3.2 |
| pt-dialogue3 | settle-off-01 | 2.1 | 2.3 |
| pt-dialogue3 | presented-off-01 | 3.1 | 3.2 |
| pt-dialogue3 | settle-off-02 | 2.1 | 2.3 |
| pt-dialogue3 | presented-off-02 | 3.1 | 3.2 |
| pt-dialogue3 | settle-on-02 | 4.7 | 5.3 |
| pt-dialogue3 | presented-on-02 | 3.1 | 3.2 |
| pt-dialogue3 | settle-on-03 | 4.7 | 5.3 |
| pt-dialogue3 | presented-on-03 | 3.1 | 3.2 |
| pt-dialogue3 | settle-off-03 | 2.1 | 2.3 |
| pt-dialogue3 | presented-off-03 | 3.1 | 3.2 |
| pt-dialogue3 | settle-off-04 | 2.1 | 2.3 |
| pt-dialogue3 | presented-off-04 | 3.1 | 3.2 |
| pt-dialogue3 | settle-on-04 | 4.7 | 5.3 |
| pt-dialogue3 | presented-on-04 | 3.1 | 3.2 |
| pt-dialogue3 | settle-on-05 | 4.7 | 5.3 |
| pt-dialogue3 | presented-on-05 | 3.1 | 3.2 |
| pt-dialogue3 | settle-off-05 | 2.1 | 2.3 |
| pt-dialogue3 | presented-off-05 | 3.1 | 3.2 |
| pt-dialogue3 | settle-off-06 | 2.1 | 2.3 |
| pt-dialogue3 | presented-off-06 | 3.1 | 3.2 |
| pt-dialogue3 | settle-on-06 | 4.7 | 5.3 |
| pt-dialogue3 | presented-on-06 | 3.1 | 3.2 |
| pt-dialogue3 | settle-on-07 | 4.7 | 5.3 |
| pt-dialogue3 | presented-on-07 | 3.1 | 3.2 |
| pt-dialogue3 | settle-off-07 | 2.1 | 2.3 |
| pt-dialogue3 | presented-off-07 | 3.1 | 3.2 |
| pt-dialogue3 | settle-off-08 | 2.1 | 2.3 |
| pt-dialogue3 | presented-off-08 | 3.1 | 3.2 |
| pt-dialogue3 | settle-on-08 | 4.7 | 5.3 |
| pt-dialogue3 | presented-on-08 | 3.1 | 3.2 |
| pt-dialogue3 | settle-on-09 | 4.7 | 5.3 |
| pt-dialogue3 | presented-on-09 | 3.1 | 3.2 |
| pt-dialogue3 | settle-off-09 | 2.1 | 2.3 |
| pt-dialogue3 | presented-off-09 | 3.1 | 3.2 |
| pt-dialogue3 | settle-off-10 | 2.1 | 2.3 |
| pt-dialogue3 | presented-off-10 | 3.1 | 3.2 |
| pt-dialogue3 | settle-on-10 | 4.7 | 5.3 |
| pt-dialogue3 | presented-on-10 | 3.1 | 3.2 |
| pt-dialogue3 | settle-on-11 | 4.7 | 5.3 |
| pt-dialogue3 | presented-on-11 | 3.1 | 3.2 |
| pt-dialogue3 | settle-off-11 | 2.1 | 2.3 |
| pt-dialogue3 | presented-off-11 | 3.1 | 3.2 |
| pt-dialogue3 | settle-timing-off-cost | 2.1 | 2.3 |
| pt-dialogue3 | settle-timing-on-cost | 4.7 | 5.3 |
| pt-s8 | settle-off-a | 2.1 | 2.2 |
| pt-s8 | presented-off-a | 3.1 | 3.2 |
| pt-s8 | settle-on-a | 4.7 | 5 |
| pt-s8 | presented-on-a | 3.1 | 3.2 |
| pt-s8 | settle-off-b | 2.1 | 2.2 |
| pt-s8 | presented-off-b | 3.1 | 3.2 |
| pt-s8 | settle-on-b | 4.7 | 5 |
| pt-s8 | presented-on-b | 3.1 | 3.2 |
| su1-ultra-s8-fg2 | diag-omm | 9.1 | 16.4 |
| wsf1-s8-interface-column | partition-static-0 | 14 | 11.9 |
| wsf1-s8-interface-column | partition-static-1 | 14 | 11.9 |
| wsf1-s8-interface-column | partition-static-2 | 14 | 11.9 |
| wsf1-s8-interface-column | partition-static-3 | 14 | 11.9 |
| wsf1-s8-interface-column | restore | 5 | 1.1 |

Historical replay coverage for active jobs (exact failing gate messages remain in the JSON):

| Job | Missing expect patterns | Other unsatisfied gate/window checks |
| --- | ---: | ---: |
| combined-Campsite---27h | 2 | 82 |
| combined-Test-Dialogue3 | 0 | 36 |
| combined-Water-Queens-House | 5 | 484 |
| cp4-compact-camp-join | 2 | 2 |
| hc1-dialogue1-fg2 | 3 | 5 |
| hc2-campsite-fg2 | 9 | 31 |
| hv1-dialogue1 | 0 | 6 |
| hv2-camp | 1 | 14 |
| hv3-s8 | 1 | 18 |
| hv4-gpuav | 0 | 9 |
| hv5-reload-bisect | 0 | 4 |
| pt-dialogue3 | 0 | 75 |
| pt-s8 | 0 | 18 |
| su1-ultra-s8-fg2 | 5 | 40 |
| wsf1-s8-interface-column | 0 | 60 |

Validation: 147/147 full CTests pass (150.64 s), with the existing shader helper using a workspace cache. 43 live-flow and 15 live-guard regressions pass. Settings test rebuilt after detecting a stale executable. Full CTest log: `captures/offline/fps-windows-ctest-final.log`.
