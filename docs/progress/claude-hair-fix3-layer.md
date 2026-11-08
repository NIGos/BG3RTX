## Strand hair layer review round 3 (branch claude/hair-fix3-layer, READY_FOR_LIVE, 2026-11-08)
- H2 (the flash on every LOD change): a slot keeps its published key through the LOD switch frame; the draw slot, the
  swap and the coverage evidence survive it. Likely the user's dialogue/camera flash.
- M1: previous points only when last updated in the previous build frame. M2: card-mode pixels get the card's motion
  vector in the swapped velocity pass. M4: non-hair draws leave the hook early. M5: clones built outside the lock, the
  unused generic G-buffer variant dropped. M6: one LOD read and validated per upload.
- L1 header tag zeroed at a pixel-record reallocation; L3 per-key maps pruned after 600 frames; L4 submit_decision
  unit-tested; L5 cfg reads once per publish. L2 left (reason in docs/RTX_HAIR_PRIMARY.md 5k).
- Gates: build -Test; shader identity: 217/221 identical, only the hair G-buffer and velocity requests differ; variants.txt delta 0.
- Cost: unmeasured (no game). Card-mode velocity adds one marker read per swapped velocity fragment.
- User check: Test Dialogue1, zoom and pan so heads cross LOD thresholds: no strand-card flash; `[hair groom]` LOD
  switches counted, `header safety` card-draw upgrades not rising with them.
