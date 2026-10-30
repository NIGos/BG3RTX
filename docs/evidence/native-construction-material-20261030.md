# Shared construction material: native recipe

Material `a1a20440-b6f8-0446-bc9d-8b92545b637d` is
`WALL_GEN_Glasshouse_Bronze_A_Multi_A`, not a VT material. Its actual Shared.pak
MaterialBank node selects `ENV_Tileset_MultipleTextures_AlphaTest.lsf`.
The priority-resolved Materials.pak template SHA-256 is
`6456680878a31ab98f7d9e9a55f2ad6748cd738df331e7b157f74e1f227c92cf`.
Full material node, nine enabled texture resources, DDS hashes, decoded point
codes and native module hashes are in `tests/native_tileset_surface_reference.json`.

Native Deferred ST pairs VS `af1d30a11685c516` with FS `7621aebd092c3889`;
STI pairs VS `a618ddae98c2d1cf` with FS `cad41eff979970e4`.
Both vertex programs pass color and UV to fragment locations 0 and 1.
The fragment selects Green when interpolated color.y > 0, otherwise Red when
color.x > 0, otherwise Base. It does not blend these three materials.
Normal textures occupy set 1 bindings 2/3/4, albedo 5/6/7, physical 8/9/10.
All use sampler set 0 binding 7. Selected UV is TEXCOORD0 times the material
tiling float at byte 0/4/8. Reflectance is byte 12. Material overrides for all
three tilings and Reflectance are disabled: authored defaults are 1 and 0.5.
There is no enabled virtual texture. The unnamed fallback Texture2D is disabled.

Native normal unpack is normalize(2*A-1, 2*B-1, 1-2*G), then native frame
columns (T,N,B). Physical R is metalness, G roughness, and B becomes the albedo
target alpha. Reflectance, roughness, metalness and albedo RGB are clamped.
Both Deferred fragments contain no discard. Their names do not prove depth
coverage, which remains unknown here.

Actual floor exports have constant COLOR0=(0,1,0,1), selecting Green at every
interior point. Wall exports contain Green, Red and a few Base vertices; their
interior selection must use interpolated color, not object names or vertex votes.
The helper supports the exact strict-positive boundary and rejects missing color,
wrong shader pairs and nonfinite selected UV/tiling. It reuses the already tested
post-sampling field arithmetic without inventing a VT validity marker.

All nine DDS files were extracted and decoded locally. Authored texture records
mark three albedos SRGB=true and all normals/physical maps false. This is not
proof of the actual native sampled view, sampler, filtering, mip selection or
residency. The numeric point fixtures test field arithmetic only; they perform
no guessed sRGB conversion. Native world normal basis equivalence and runtime
shader/material association are also unqualified.

Validation: 230 new CPU checks, including exact zero/nextafter branch boundaries,
Green precedence, selected-only tiling validation, both shader pairs, missing
view/sample/scalar refusal and three actual normal/physical point-code fixtures
against double arithmetic. Existing VT test still passes all 164,188 checks.
No CMake, runtime integration, shader, ABI or settings change; no GPU/live work.
