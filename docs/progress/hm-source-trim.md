## Source copies freed once their frame's queries retired; IP grass verdict (branch hm-source-trim, READY_FOR_LIVE, 2026-11-02)
- Cause: every scene slot (8) kept its source copy until it was prepared again. hig1 10:27: source 2144 MB in 8
  slots with IP grass on (512 MB off), with at most 2 queries alive. Only queries of the slot's own frame read it.
- Fix: collect() returns a slot's copy to the recycle pool once its frame moved on and its owners retired
  (source_releasable, native_hit_resources.h, unit test). The streams line now prints slots holding a copy and the
  pool. Predicted: off 512 -> about 128 MB, on 2144 -> about 550-800 MB (2 live slots + up to one in the 512 MB pool).
- IP grass cost (+1.9 ms GI trace, cost windows): no new traversal path (same TLAS, same alpha test); it is per-hit
  work on newly qualified grass hits: the instanced clip check (2 x 3 vertices), 3 texture layers, and the light and
  sun shadow rays of gi_hit_direct / gi_hit_sun, which traverse the alpha-tested grass. Inherent to qualifying those hits.
- Verdict: keep rt_hit_material_ip_grass off (not in Ultra): +1.9 ms and about +1.5 GB VRAM after this trim, for
  +3 points of share (the 42.6% line covers few lookups) and no measurable energy change (+0.19%, band 2.19%).
  Revisit after OMM (the shadow rays' alpha cost) or in a scene where it changes the image.
