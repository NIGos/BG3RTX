## Direct sun footprint (branch water-glint, blocked offline, 2026-11-03)
- No production filter or setting shipped: the native-BRDF footprint gate rejects the sampled candidate.
- Native phase samples are not footprint moments; mip-vector lengths do not prove slope variance.
- Explicit spatial sampling preserves the native sun kernel, but fixed quadrature needs a convergence proof.
- The reproducible normal-map reference and S8 static/held Q/E draft are retained; nothing queued or run.
- N, E, reflection roughness, coverage split and record layout remain unchanged; request delta 0.
- Candidate cost is unmeasured; 4 x 4 already needs 96 extra normal fetches, beyond the original estimate.
- See WATER_SURFACE_ANALYSIS.md for the deciding reference numbers and remaining implementation gate.
