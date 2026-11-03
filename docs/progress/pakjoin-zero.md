## Pak join participation (branch pakjoin-zero, READY_FOR_LIVE, 2026-11-03)
- Cause: discovery required the HLOD-only fallback; camp instead logs 306-308 class-A draws/frame. Readback also filtered by HLOD publication eligibility.
- rt_hit_material_paks stays off by default; enabled, it probes keyed pool draws. Pooled recipes may use bounded retired UV readback after basis capture; captured and instanced rows retain precedence.
- Shared compact v4 validates: 40,983 rows, 41,956,440 bytes. No generator/index fix, content-ID dependency, ABI or shader request delta.
- Camp prediction after layer restart with the key enabled: cumulative draws delta 36,720-36,960/120 frames; UV attempts delta 1-36,960 after readbacks retire. Zero is a failed participation gate.
- Full witness/recipe/cull/resource/basis gates stay mandatory; exact rows may remain 0 (retained camp replay: 58 unique UV candidates/471, 0 full witnesses).
- Host DLL builds; five focused CTests pass; C++ parses the real v4 table; diff check clean. No game launched, git metadata writes, coverage promotion or GPU cost measurement.
