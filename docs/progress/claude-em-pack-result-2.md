## gi_far_roof_emission against the PT with emission: CLOSER (branch claude/em-pack-result-2, 2026-11-21)

- Pack replay-20261121-071523-1 (inn, Ultra, OMM off, 1579 MB, frame 2077), test-main a4b740f8 binaries; the scene
  handout stamp now holds the replay's TLAS (no device loss). Emissive scene: 24 records, 98647 triangles.
- Fidelity, 1054=0 replay vs live giRaw: bit-equal 94.43% of components, mean |diff| 0.0105 (live runs the raygen pass,
  the replay compute).
- 1054=1 changes 1128 of 2731520 GI pixels; 0 raised, giRaw.a unchanged. On them giRaw luminance 0.4425 (off) -> 0.0042
  (on), no-emission 0.0020; emission term 0.4405 -> 0.0022 against the PT's 0.0379 (hidden first-hit emission x fade,
  casters as roofs).
- Mean |E - PT| on the changed pixels: 0.4061 (off) -> 0.0386 (on); 55 of 55 tiles closer. PT at 64 iterations: 0.4059
  -> 0.0385, same verdict.
- Whole frame (offline, one frame, no denoiser): mean giRaw luminance -2.98% (0.006083 -> 0.005902): the dropped
  emission is a few very bright hidden hits seen through the cut roof. Live r2-inn-roof-em: energy +0.25% (within noise),
  GI GPU ms equal.
- Verdict per predicted.json: CLOSER. Default change prepared on claude/roof-em-on (not merged). Summary:
  claude-em-pack-result-2.json; the pack and the replay outputs are deleted.
