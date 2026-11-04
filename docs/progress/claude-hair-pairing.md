## Coverage pairing with history, keyed by the prepass frame (branch claude/hair-pairing, READY_FOR_LIVE, 2026-11-04)

- hv7 10:25: the count found strand pixels (evidence from tag - 1 / - 2) but every draw read 0/0: card and strand
  samples were kept newest-only per key and arrived with different latencies, so their frames never met.
- Now hair_primary::CoveragePairing keeps 16 frames per key and side; the G-buffer frame records the prepass slot
  recorded before it (card_frame) and the strand pixels pair on it. Evidence age bound: 12 frames (card readback 4 +
  strand count up to 3 tags behind + 3 publishes + 2). The strand-traces line logs G-buffer frame, card frame, keys and
  pairs. Unit test on the real ordering.
