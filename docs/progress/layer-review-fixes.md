## Layer review fixes (branch layer-review-fixes, kept, 2026-11-03)
- OMM allocates stats/readback before pipeline publication and the fast path; partial failures retry independently.
- Shader entries retain replaced SPIR-V allocations until process exit so async pipeline workers keep valid blobs.
- Water compose records always allow transfer-source copies when pan_water is enabled later.
- Build passed; all 144 CTests passed (178.79 s). ABI unchanged; shader request delta 0; no game launch.
- No commit or git metadata writes. Retired shader memory grows only on successful shader replacements.
