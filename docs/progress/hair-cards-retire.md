## Native hair card cleanup (branch hair-cards-retire, VERIFIED, 2026-11-03)
- Retired visibility (null), cheap and GI cull (energy failures), quarantined mean; retained settings/presets match main exactly.
- Main 71f92bd3: 139/260 surviving requests byte-identical; all other 121 requests / 140 modules equivalent with retired SpecIds 1017-1019 fixed at 0, other constants preserved (shared spec_cascade_check canonical comparator).
- Both sides: 297/297 module entries validated, zero errors; retired IDs never reused, ABI unchanged.
- shaders/variants.txt: 165 -> 165 requests (delta 0); total unique requests 261 -> 260 only because rt/hair_mean.hlsl was deleted.
- Defaults regenerated and checked; retired keys absent from defaults/GUI/schema; legacy INIs with all four keys 0 or 1 load unchanged in every preset (settings_test: 3,865 checks, 0 failed).
- Full build passed; 145/145 CTests passed in 154.70 s, BelowNormal and serial; VS Installer directory added to PATH for vcvars discovery.
- Evidence: build/hair-retire-identity.json, build/hair-retire-identity-current.log, build/hair-retire-final-build-test.log. No game launch, commit or git metadata writes; mean device-loss cause remains unknown.
