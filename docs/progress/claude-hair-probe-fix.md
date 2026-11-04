## Strand probe and G-buffer trace only this frame's groom TLAS (branch claude/hair-probe-fix, READY_FOR_LIVE, 2026-11-04)

- hv5 lost the device in the load with no strand swap (the probe was the only groom trace in the game's command
  buffers); hv6 probes counted 0 strand samples next to 95670 card samples. Both traced a staged publish up to 2 frames
  old: the groom TLAS is camera-relative (an older one misses the strands) and can reference BLAS ranges freed by a
  LOD change or compaction after the scene slots retired.
- Probe and order-mode G-buffer header now take only the publish of this frame (else no probe / cards); no probe during
  the 300-frame load window; groom BLAS, points and LOD frees wait 3 more frames (bvhsplit kGroomFreeLag).
- The coverage line ends with "probe frames refused: no publish of the frame N, older publish N, load quiet N".
- Live check: hv6 (no device lost in the load, strand samples > 0 for drawn hair, refusal counts).
