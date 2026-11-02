## Hit-material stream caps from the RT capture (branch hm-stream-caps, READY_FOR_LIVE, 2026-11-02)
- Cause: at camp the fixed caps (deferred 4 x 32 MB, source 128 MB, basis 256 MB) overflowed with IP grass on: 119-134
  deferred rows dropped, wind / wind RGB captured 0, qualified share 26.4% -> 24.3% (hig1 09:54).
- Fix: native_hit_resources.h stream_caps from the capture size (cap.xfb_size at scene_prepare): source = the
  capture's wide-stride alpha floor share (40%, capture_budget::kAlphaFloorPctWide) in 32 MB granules, deferred chunks
  = source / 32 MB + 1, basis = 2 x source; the old caps for small or unknown captures. Ultra (1024 MB capture):
  source 384 MB, deferred 13 chunks (416 MB), basis 768 MB. Buffers stay sized to the need: VRAM grows only by use.
  New line `[hit material streams] frame N live X MB: source / deferred / query basis` reports what they hold.
  Unit test: native_hit_pipeline_test (stream_caps).
- Predicted VRAM at camp with IP grass on: about +55 MB source per slot, +55 MB deferred per packet, +110 MB basis
  per query in flight over hig1 09:54 (the evicted wind rows); measured by the streams line.
- hig1 job: wind and wind RGB captured > 0 with stream 0 in diag-on, capacity no overflow in every diag window
  (event, settle_s 1). Old log fails them (the eviction), as it should. Layer change: restart.
