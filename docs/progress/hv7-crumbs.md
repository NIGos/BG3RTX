## hv7 header/probe bisect (branch hv7-crumbs, OFFLINE_PREPARED, 2026-11-04)
- Replaced failed warm-order-cards with order-0 warm-all, then order-1 masks 0/8/15; two complete 300-frame periods each (92.6 s), probe-bit coverage gates and per-window fault breadcrumbs; estimate 7.1 min including load.
- live_flow, batch lint (6), shared-flow tests (61) and fake-queue dry-run pass; no game launch or commit. Runtime dependency: periodic crumbs currently use [hair primary], while this job requires [fault].
