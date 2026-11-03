# VRAM allocation audit

Read-only source audit, 2026-11-02. Source revision: `855c99d0dbf947c7b63fc6b395204737bbaa7afc`
on `codex-vram-audit`. No build, shader compilation, test, game launch, INI change or
runtime experiment was performed. Only this document is changed. Main-checkout queue logs
were read as explicitly requested; no capture or shared asset was changed.

## Interpretation and limits

Tables enumerate owned allocation families, including conditional diagnostics and experimental
features. Repeated objects are named together with their multiplicity; an AS handle is not
an extra allocation on top of its backing buffer. Buffer/image sizes below are payload estimates,
not measured committed memory: Vulkan `VkMemoryRequirements::size`, alignment, image tiling,
driver metadata and temporary overlap during deferred destruction add overhead.

All MB figures in this document mean **MiB (2^20 bytes)**, matching the log formatter.
Column A means render **2560 x 1067**, output **3840 x 1600**. Column B means UHD output
**3840 x 2160**, with the same DLSS Quality linear scale, render **2560 x 1440**.
Ultra does not select the game's DLSS render resolution. At native UHD render, substitute
8,294,400 render pixels in every formula (2.25 times B's render-dependent values).
Let `R = render width * render height`, `O = output width * output height`;
one byte per pixel is A/B **2.605 / 3.516 MiB** at render and **5.859 / 7.910 MiB** at output.
Scene buffers do not scale with resolution; their A/B values are identical for the same scene.
Zero means not allocated in a fresh ordinary Ultra session, not necessarily released after toggling off.
`?` means no defensible numeric estimate is available; the formula is supplied instead.

`[mem]` (`layer/layer.cpp:535-560`) sums device-local heap usage and budget through
`VK_EXT_memory_budget`; it has no allocation attribution. Every resident device-local row
contributes to that aggregate, including game and SDK allocations. The table's Log column
therefore describes **additional** attribution/capacity logs, not a separate heap.
Host-visible buffers without a DEVICE_LOCAL requirement are classified separately below:
the selected memory type determines whether they occupy VRAM. A CPU mapping does not prove
system RAM. Driver-owned descriptor/query/command pools have no public byte size.

The reported user session rose from about 22 GB to 31.7 GB against a 30.6 GB budget.
Those figures are user observations, not a reconstructed allocation total. Known chunk and
cascade growth alone cannot explain the entire rise; the audit does not assign the remaining
growth to a consumer without evidence. The separate layer budget-guard change is outside scope.

## Layer image and presentation allocations

For these rows, images are one mip, one layer, one sample unless explicitly stated.
`replace` means old objects enter deferred destruction: `layer/hotreload.cpp:442-493`
requires at least four presents and GPU completion through frame + 2, with a 240-frame fallback.
Replacement can therefore temporarily retain both sizes. Settings-off alone is not a free rule.

| Source / consumer | Payload formula; A / B MiB | Allocate; growth; shrink/free | Need/reserve; multiplicity | Additional log |
|---|---|---|---|---|
| `layer/ngx.cpp:498,548-562` RR diffuse, specular, normal/roughness guides | `R*(4+8+8)`; **52.10 / 70.31** | First guide use; replace all on dimension change; no off-key reclamation at this site | Sized to resolution; one set of three, not per frame | `[ngx] guide buffers` dimensions/IDs, no byte capacity |
| `layer/rrpost.cpp:643-646` Xs, Xr, AC, EB | `R*(4+4+8+8)`; **62.52 / 84.38** | First post-chain; replace individual images on dimensions/format change; retained when RR/post is disabled | Sized to resolution; one of each | `[rrpost]` name/dimensions/format/ID |
| `layer/rrpost.cpp:1003` Z before output grade | `8*O`; **46.88 / 63.28** | First successful RR post take; replace dimensions; retained off | Need; one | `[rrpost]` image creation |
| `layer/rrpost.cpp:836,859` SSS-before and SSS guide | `R*(4+2)`; **15.63 / 21.09** | On demand `rr_sss`; replace dimensions; no off-key free | Need; one each | `[rrpost]` creation |
| `layer/rrpost.cpp:873` colour before transparency | `4*R`; **10.42 / 14.06** | On demand `rr_cbt`; replace dimensions; retained off | Need; one | `[rrpost]` creation |
| `layer/rrpost.cpp:808` diagnostic grade check | `8*R`; **0 fresh Ultra**, if tracing/check enabled **20.84 / 28.13** | `rr_post_check` or tracing; replace dimensions; retained after check ends | Need; one | `[rrpost]` creation, no retained-byte total |
| `layer/rrpost.cpp:296-297` dummy 2D/3D | Two RGBA16F texels, **0.000015** payload (allocation granularity larger) | First post layout; never explicitly reclaimed by `img_destroy` except replacement | Fixed; two images | Creation log |
| `layer/streamline.cpp:668,712-738` FG private MV/depth input copies | Each used slot `R*(4+4)`; **20.84 / 28.13 per slot**, **166.72 / 225.00** at eight | First use of each available lease; replace slot dimensions after SDK input-consumed fence; shutdown frees (`:1789-1804`); FG key off does not immediately release | Need per image, fixed maximum eight slots, allocated lazily; actual live count unknown | `[sl] input copy slot` index/dimensions/format; no byte total |
| `layer/streamline.cpp:1955-1984` UI PQ tag | `8*O`; **0 fresh Ultra**, enabled **46.88 / 63.28** | `fg_ui_pq` on demand; deferred replace dimensions; retained off | Need; one image (eight descriptors do not mean eight images) | `[sl] fg_ui_pq` dimensions |
| `layer/hotreload.cpp:390-424` core image allocator | `width*height*format bytes`; counted under each core consumer below | Per caller; deferred free by core | Helper, **not additive** | No allocation ledger |

## Diagnostic staging and opaque allocations

| Source / consumer | Payload formula; A / B MiB | Allocate; growth; shrink/free | Need/reserve; multiplicity | Additional log |
|---|---|---|---|---|
| `layer/dump.cpp:95-119,629-638` image dumps / pan pose | Sum captured `w*h*texelBytes`, plus 224 bytes per pose; **0 fresh Ultra**, scene/diagnostic dependent | On demand; pan accumulated payload limited to 6 GiB (`:603`); individual completion/deferred retirement | Need per pending dump; host-cached preferred, **possibly** device-local fallback | Dump/pan diagnostics, no attributed resident VRAM total |
| `layer/replay.cpp:1216-1265` replay pack staging | Aligned sum copied buffer/image bytes, partitioned by `p.chunk_bytes`; **0 fresh Ultra**, ? while capture pending | On-demand replay; `release_chunks` on completion/failure | Need; per pending job/chunk; host-cached preferred, possibly device-local fallback | `[replay]` copy/read bytes, not necessarily resident heap bytes |
| `layer/streamline.cpp:328-350` presented snapshot buffer | `4*O`; **0 fresh Ultra**, if requested **23.44 / 31.64** | Snapshot request; associated command-pool/family setup; reusable singleton retained after snapshot request | Fixed at creation extent; host-visible memory, device-local membership depends on type | `[sl] capturing` |
| `layer/rrpost.cpp:298,977` dummy/probe host buffers | 256 bytes + `kProbeFloats*4`; <0.01 both | First layout / exposure probe; retained | Fixed singleton; host-visible, device-local only if selected type has flag | Exposure probe, no capacity |
| `layer/ngx.cpp:290`, `layer/rrpost.cpp:283,304`, `layer/streamline.cpp:328,742,1921`; geometry/core pool sites listed below | Descriptor/query/command pools: **? / ?**, implementation-owned storage, no Vulkan payload formula | Lazy subsystem initialization; some replacements deferred; some kept until device shutdown | Pool counts bound descriptors/queries, **not** MiB or image multiplicity | No byte attribution |
| `layer/ngx.cpp` NGX RR/SR feature and `layer/streamline.cpp` DLSS-G SDK | **? / ?** proprietary SDK feature memory, histories, internal images/pools; depends on SDK/render/output/feature lifecycle | SDK create/evaluate/release; not enumerable from repository buffer calls | SDK-owned; do not equate owned-input image table with entire RR/FG cost | `[mem]` aggregate only; SDK logs are not allocation accounting |
| `layer/tracker.cpp:632,890`, `layer/geocapture.cpp:402`, `layer/rtscene.cpp:1574`, `layer/replay.cpp:94,131` intercepted game allocation calls | **? / ?** game/other-layer memory | Forwarded/tracked, not new BG3RTX-owned buffers/images | Excluded from owned-resource totals; included in process heap pressure | Tracker metadata and aggregate `[mem]` |

`layer/hotreload.cpp:362-387` is the common host-buffer helper. Its memory flags require
HOST_VISIBLE and HOST_COHERENT, not DEVICE_LOCAL; all callers are conditional-VRAM rows,
not guaranteed zero-VRAM allocations. Explicit device-local geometry staging is included below.

## Core allocations

Core image creation uses `layer/hotreload.cpp:390`. Unless a row says otherwise, allocation is
on the first matching rendering pass, resources persist with the key off, and core release/hot
reload defers destruction. Image dimension changes replace old images in both directions.
Ultra with RR active uses `gi_res_shift=0`: GI histories have full render resolution, not half.
Let `G=R` with RR; otherwise use the actual shifted GI dimensions from `core/gi.cpp:548`.

| Source / consumer | Payload formula; A / B MiB | Allocate; growth; shrink/free | Need/reserve; multiplicity | Additional log |
|---|---|---|---|---|
| `core/gi.cpp:554` raw GI | `8*G`; **20.84 / 28.13** | GI prepare; replace size/shift; release `:528-542` | Resolution; one | GI dimensions, no capacity bytes |
| `core/gi.cpp:555-556` GI history | `2*8*G`; **41.68 / 56.25** | Same | Resolution; two temporal ping-pong images | Same |
| `core/gi.cpp:557` raw depth | `4*G`; **10.42 / 14.06** | Same | Resolution; one | Same |
| `core/gi.cpp:558-559` history depth | `2*4*G`; **20.84 / 28.13** | Same | Resolution; two temporal images | Same |
| `core/gi.cpp:560` previous radiance | `8*R`; **20.84 / 28.13** | Same | Resolution; one | No byte capacity |
| `core/gi.cpp:561` RT visibility | `4*R`; **10.42 / 14.06** | Same | Resolution; one | No byte capacity |
| `core/gi.cpp:562` origin | `8*R`; **20.84 / 28.13** | Same | Resolution; one | No byte capacity |
| `core/gi.cpp:563` auxiliary | `8*R`; **20.84 / 28.13** | Same | Resolution; one | No byte capacity |
| `core/gi.cpp:564` no-light dummy | 16 bytes; **<0.001 / <0.001** | Same | Fixed one texel | None |
| `core/gi.cpp:565` reflection hit distance | `2*R`; **5.21 / 7.03** | Same; allocated with GI image set | Resolution; one | No byte capacity |
| `core/gi.cpp:567` physical hair origins | `16*R`; **0 / 0** fresh Ultra, enabled **41.68 / 56.25** | Startup capability + `rt_hair_physical_origin`; retained off; resize/release | Resolution; one | `[hair origin]` bytes |
| `core/gi.cpp:768,828` box / RF hit-GI statistics | `32*64*4` and `8*64*4`; **0 / 0** fresh Ultra; enabled **0.0078 + 0.0020** | First corresponding off-by-default feature; release `:3401,3427` | Fixed singleton buffers | Stats values, no capacity |
| `core/gi.cpp:1378` RT statistics readback ring | `kStatsCounters*64*4*8`, <=**0.875** at 448 counters | First statistics; release core; no resize | Fixed eight regions; conditional device-local host allocation | RT/LS counters, no resident-byte ledger |
| `core/water.cpp:310-311` reflection RGBA16F + RG32F | `16*R`; **41.68 / 56.25** | First reflections; resize `:306-308`; `water_release :1545` | Resolution; one each | `[water] reflection images` MB |
| `core/water.cpp:540-541` compose radiance/depth | `12*R`; **31.26 / 42.19** | First compose; resize `:536-538`; release | Resolution; one each | `[water] compose images` MB |
| `core/water.cpp:528` glint AA | `8*R`; **0 / 0**, enabled **20.84 / 28.13** | First `rt_water_glint_aa`; resize `:519`; compose resize/release frees | Resolution; one | `[water] glint image` |
| `core/water.cpp:1272` private water MV | `2*4*R`; **0 / 0**, enabled **20.84 / 28.13** | First RR/FG water MV override; resize `:1269`; `release_mv :1220` | Resolution; two parity images | MV diagnostics, no capacity |
| `core/water.cpp:174,178` reflection device stats + RT stats | `256+65536`; **0.0627 / 0.0627** | First reflections; release `:1545-1559` | Fixed one each | Reflection stats, no capacity |
| `core/water.cpp:179` reflection host stats/parameters | `8*(256+256)`; **0.0039 / 0.0039** | First reflections; release | Fixed eight regions; conditional device-local | Stats only |
| `core/water.cpp:602` compose GPU stats/parameters | `8*(512+256)+768`; **0.0066 / 0.0066** | First compose; release `:1167` | Fixed buffer with eight regions | Compose stats only |
| `core/water.cpp:607,1266` compose readback / MV parameters | `8*(512+256)` + 192 bytes if MV active; **0.0059 / 0.0059** + <0.001 | First respective use; release | Fixed host buffers; conditional device-local | No capacity |
| `core/ls_lights.cpp:108` light match slots | `4096*3*16`; **0.1875 / 0.1875** | First light matching; free `:244-250` | Fixed one image | Light statistics, no image bytes |
| `core/ls_lights.cpp:107` light upload | `65536*8`; **0.5 / 0.5** | First matching; release | Fixed eight regions; conditional device-local | No capacity |
| `core/sun_trust.cpp:71` device trust image | `8192*4`; **0.03125 / 0.03125** | First trust; release `:303-305` | Fixed one | Trust logs, no bytes |
| `core/sun_trust.cpp:69` trust upload | `32768*8`; **0.25 / 0.25** | First trust; release | Fixed eight regions; conditional device-local | No capacity |
| `core/emissive.cpp:100` debug/calibration GPU counters | `(16+1024*16)*4`; **0.0626** when needed, otherwise 0 | First debug/calibration; `em_release` frees | Fixed one | Emission check counters, no capacity |
| `core/sharc.cpp:126-129` hash / accumulation / resolved / optional lock | `N*(8+16+16+[4 if lock fallback])+96+8208`, `N=2^clamp(sharc_log2,16,24)`; **0 / 0** Ultra; default enabled `N=2^22`: **160.008**, **176.008** with locks | First SHaRC; resize on exponent/lock change `:283-288`; off retains; `release_buffers :106`, `sharc_release :383` | Fixed capacity reserve chosen by key; one each | SHaRC diagnostics, no per-allocation heap ledger |
| `core/sharc.cpp:294` cache readback | `8*2048*4`; **0 / 0**, enabled **0.0625** | First SHaRC; release | Fixed eight regions; conditional device-local | Counters only |
| Core descriptor/query pools: `core/gi.cpp:359,446,744,800,861,2478,2525,2723`; `core/water.cpp:193,619`; `core/emissive.cpp:108`; `core/sharc.cpp:138,303` | **? / ?** implementation allocation, no explicit memory requirement exposed | First subsystem/query use; release/defer via module teardown | GI timestamps 32; RT 16; RF 16; LS 24; shadow 16; water 16/80; emission 24; SHaRC 32. Descriptor pools contain sets, not duplicate images | No byte capacity |

The ordinary GI image set is **66*R = 171.94 / 232.03 MiB** with RR, plus tiny dummy
allocation overhead. The two GI history pairs alone are 24*R. They are not removable by merely
skipping allocations: `shaders/gi/gi_temporal.hlsl:65` still writes the current history output,
and composite consumes it. A bypass must also change bindings and handle runtime RR transitions.

## Layer geometry, acceleration structures and capture

All values in this section are **A = B**, for identical scene demand. `ASsize` and `scratchSize`
mean the driver result of the corresponding Vulkan build-size query, not triangle payload size.
`align` is the alignment applied at the cited call site. `BAR` means device-local host-visible
memory preferred with host-only fallback. Geometry copies and AS backing are separate consumers.

| Source / consumer | Payload formula; A = B MiB | Allocate; growth; shrink/free | Need/reserve; multiplicity | Additional log |
|---|---|---|---|---|
| `layer/geocapture.cpp:466` primary RT XFB | `max(effective rt_xfb_mb,16)`; default 384 scaled by `64/24` material stride => **1024** | First capture; fixed startup size; teardown `:2748`, no shrink | Fixed reserve; one shared buffer | `[geo] RT capture` demand/drops, initial size |
| `layer/geocapture.cpp:467` draw counters | `4*max(geo_max_draws,256)`; default 16384 => **0.0625** | First capture; teardown, no shrink | Fixed; one | Geo draw counts |
| `layer/geocapture.cpp:1137` cascade XFB | `3*max(effective rt_cascade_mb,g_cc_grown)`; 160 scaled by 64/24 and rounded => **3*427=1281**; reported growth **3*544=1632** | First cascade; overflow `:1424` grows to demand +25%, rounded 16 MiB, cap 2047 per buffer; never shrinks; teardown | Fixed initial reserve plus high-water; **three slots** | `[cascade]` capture/grow; logs per slot, not aggregate |
| `layer/geocapture.cpp:1053,1147` current-sun cascade capture/private copies | Up to `15*effective cascade bytes`; **0** Ultra; default enabled up to **6405** | On-demand `rt_sun_cascade_current`; four-by-three private capture slots plus legacy copies; no demand shrink | Fixed reserve per slot | Current cascade logs, incomplete aggregate |
| `layer/geocapture.cpp:425,2553` capture readback / UBO/image copies | Requested copied bytes; **?**, diagnostics dependent | On-demand; retired after copy consumption | Need; host-cached/visible, conditional VRAM | Capture diagnostics, no residency attribution |
| `layer/bvhsplit.cpp:266` static BLAS backing chunks | Sum `max(rt_mesh_chunk_mb*2^20,aligned AS request)`; default **>=64*chunkCount** | First mesh builds; append chunks; ranges retired after GPU-safe delay (`:2565`), **whole chunks never freed until teardown `:995`** | Chunk reserve/fragmentation; global pool, not per frame | `[bvh] AS` reports **live ranges**, plus chunk count; not committed bytes |
| `layer/bvhsplit.cpp:337` mesh position/material-basis pool | Starts `rt_mesh_pool_mb=256`; doubles to min(`rt_mesh_pool_max_mb=3072`,2047,SSBO device limit); **256..2047** | On demand; replacement deferred; ranges recycled; whole buffer never shrinks | High-water reserve; one pool. Basis cap 768 MiB is **inside**, not additional | `[bvh] mesh cache` pool used/cap |
| `layer/bvhsplit.cpp:694` instance/info/matrix ring | `8*(align256(64*ni)+align256(16*ni)+align256(48*nm))`; `ni=max(1.5*N,old,4096)`, `nm=max(1.5*M,old,1024)`; minimum **2.875** | Prepare demand; grows only; old deferred on replacement; teardown | High-water; eight regions; BAR preferred | `[bvh]` ring/capacity |
| `layer/bvhsplit.cpp:729` split TLAS backing | `sum ASsize(max(1.5*N,4096,hint))`; **?** | Per-slot prepare; high-water; teardown/replacement, no demand shrink | Sized with headroom; **eight slots** | TLAS capacity logs; driver bytes not fully itemized |
| `layer/bvhsplit.cpp:757` shared build scratch | `1.25*(need+scratchAlignment)`; **?** | First build / higher scratch demand; replace old; never shrinks | High-water; **one shared** (already not eight copies) | No dedicated capacity |
| `layer/bvhsplit.cpp:939` status | `16384*16`; **0.25** | BVH startup; teardown | Fixed one; BAR preferred | No dedicated bytes |
| `layer/bvhsplit.cpp:849-852` OMM alternate TLAS, instances, replacements, stamps | `ASsize(N)+64*N+8*N+32*ranges` per slot; **0** ordinary Ultra | OMM on demand; exact high-water buffers; teardown/replacement | Up to eight slots; metadata BAR | OMM logs, incomplete aggregate |
| `layer/bvhsplit.cpp:2207,2226,2234` current-sun instances, TLAS, scratch | `64*(baseN+4)+ASsize(baseN+4)+max(four BLAS/TLAS scratch)+alignment`; **0** Ultra | Recreated for new base record; old deferred | Up to eight slots; instances BAR. Four sun BLAS at `:2199` use chunks **already counted above** | Current-sun logs |
| `layer/rtscene.cpp:413,444,458,384` legacy BLAS/TLAS/instance/scratch | `ASsize(max(1.25*opaqueN,65536),max(1.25*alphaN,16384))+ASsize(1)+64+1.25*(scratchNeed+alignment)`; **0** normal split path | Split disabled/fallback; capacity/geometry changes replace; teardown; no routine shrink | One each; driver-size dependent | Legacy BVH capacity |
| `layer/alphatest.cpp:636` alpha records | `8*(16+8192*208)`; **13.0001** | Alpha startup; teardown `:680-716` | Fixed eight regions | Alpha capacity/record drops `:1354` |
| `layer/alphatest.cpp:639,641,1047` dummy storage/images, legacy buffer | 256-byte dummy + two 4x4 RGBA8 images (128 bytes) + optional 256-byte legacy buffer; **<0.001** payload | Startup / legacy demand; teardown | Fixed singletons | No bytes |
| `layer/alphatest.cpp:646` stamps | `8*512`; **0.0039** | Startup; teardown | Fixed host-visible; conditional VRAM | No bytes |
| `layer/alphatest.cpp:307,336` BC4 VT mask images and upload staging | Sum all loaded mips `ceil(w/4)*ceil(h/4)*8`; **?** catalogue-dependent, same amount staging | First VT-mask use `:1180`; retained images **and staging** until teardown `:696` | Need per mask/mip; staging host-visible, conditional VRAM | Mask catalogue logs, no complete residency total |
| `layer/alphatest.cpp:1581-1583,1601` current-sun records/dispatch/parameters/stamp | Per slot `16+208*records +16*draws+1232`, plus 512 host bytes; **0** Ultra | Current-sun only; exact high-water; teardown | Eight slots, stamp conditional VRAM | Current alpha capacity |
| `layer/emissive.cpp:1102,1431` emission soup | `rt_em_xfb_mb=32`; demand growth when `3*demand>size`, target `4*demand`, doubling capped `rt_em_xfb_max_mb=512`; **32..512** | First emission; no shrink; old allocations survive **16 builds** (`:1440`) then deferred retirement | High-water; one active plus retired generations | `[em] soup` / `emissive soup grown` |
| `layer/emissive.cpp:1106` emission lights | `80*clamp(rt_em_max_lights,256,2^20)`, default 65536 => **5** | Emission startup; teardown `:1154` | Fixed one | Light counts, no unused-capacity total |
| `layer/emissive.cpp:1107,1110,1114` emission host data/instances/globals | Eight aligned data slots **3.533**, eight instance/info slots **0.627**, globals **0.01564** | Startup; teardown | Fixed host-visible; conditional VRAM | No complete capacity bytes |
| `layer/emissive.cpp:1108,1115` emission dummy buffer / two images | 256 bytes + 128 image payload bytes; **<0.001** | Startup; teardown | Fixed | No bytes |
| `layer/emissive.cpp:608` emission BLAS/TLAS backing | Each slot `align256(max(1.5*BLASsize,1 MiB))+align256(max(TLASsize,4096))`; minimum about **8.03** all slots | On-demand build; high-water; teardown/replacement | **Eight slots**; need plus reserve | Soup logs do not expose these bytes |
| `layer/emissive.cpp:631` emission shared scratch | `align256(1.5*(max(BLAS build/update,TLAS scratch)+alignment))`; **?** | First/higher demand; never shrinks; teardown | One shared high-water buffer | No capacity |
| `layer/water.cpp:247,873` water soup | Starts **32**, grows toward `4*demand` when `3*demand>size`, cap **256** | On-demand water capture; no shrink; retired generations kept **eight builds** then deferred (`:876`) | One active plus retired generations | `[water]` capture/build/growth |
| `layer/water.cpp:263,266` water instances / position readback | Four*256 bytes + four*`256*8*256` bytes => **2.001** | Water startup; teardown | Four slots; host-visible, conditional VRAM | No capacity bytes |
| `layer/water.cpp:377` water BLAS/TLAS backing | Same per-slot formula as emission, but **four slots**, min about **4.02** | On-demand build; high-water; teardown | Four slots | No AS byte capacity |
| `layer/water.cpp:404` water scratch | Per slot `align256(1.5*(max(BLAS,TLAS scratch)+alignment))`; **?** | On-demand; high-water; teardown | **Four** scratch buffers | No capacity |
| `layer/water.cpp:494-496` compose records | `capacity_of_mb(record_mb(rt_water_compose_mb,stride),stride)*stride`; Ultra stride112 => about **560** (log rounds559); 5,242,878 records | First enable; fixed address embedded in patched fragment shaders; **never resize/free until teardown `:659`** | Fixed reserve; **one**, not four; independent of resolution | `[water] compose records` count/stride/MB; capture accepted/overflow |

### Hit-material allocations (all A = B)

`layer/hit_material.cpp:314-329` rounds allocations with `common/native_hit_recycle.h:23`:
minimum 64 KiB, power-of-two through 1 MiB, then 12.5% bins. Its idle recycler is capped
at **512 MiB**, with a **120-frame** TTL, and may reuse up to twice the requested size.
Capacity **used** bytes therefore do not equal allocated stream bytes. Host=true requests BAR.
Source trim already exists (`:503-512`); eight source slots must not be assumed permanently full.

| Source / consumer | Payload formula; MiB A = B | Allocate; growth; shrink/free | Need/reserve; multiplicity | Additional log |
|---|---|---|---|---|
| `layer/hit_material.cpp:751` prepass source | Accepted prepass need rounded 32 MiB; per-slot cap **384** for 1024 MiB XFB (`common/native_hit_resources.h:116`) | Prepare; replaces undersized or >2*need; retired/trimmed `:503-512` | Need plus bins; up to eight source slots | `[hit material capacity]` used/cap; `[hit material streams]` actual streams |
| `layer/hit_material.cpp:815` deferred rows | `16+4096*256`, rounded about **1.125** per packet | First deferred packet; recycle after safe retirement | Fixed rows; up to eight packets | Capacity rows, streams exclude metadata |
| `layer/hit_material.cpp:864` deferred capture XFB | **32 MiB chunks**, up to 13 per Ultra packet cap | Draw-on-demand; recording/GPU retirement then recycle/free | Need in chunks; up to eight packets | Capacity/streams |
| `layer/hit_material.cpp:855` instanced staging | `4096*512` => **2** per packet | First packet; safe recycle/free | Fixed packet reserve; eight; BAR | No complete metadata bytes |
| `layer/hit_material.cpp:1312` query rows | `32+12288*256`, optionally +`4096*256`; rounded **3.25 / 4.5** per query | Query prepare; safe recycle/free | Fixed rows; up to **16 queries** (`:1104`); BAR | Capacity row counts |
| `layer/hit_material.cpp:1313` query data | `round(max(cursor,256))`; nominal cursor cap **768** per query, potentially larger reused allocation | Query prepare; recycle-fit; safe retirement | Need plus bins; up to 16 queries | Capacity cursor vs `[hit material streams]` actual |
| `layer/hit_material.cpp:1314` pool UBO | `512*poolRows`, <=**2** payload/query | Query prepare; safe recycle/free | Need; up to 16; BAR | No dedicated resident bytes |
| `layer/hit_material.cpp:1380` remap | Counter bytes + instance permutation bytes | Query mapping; safe recycle/free | Need/query; BAR; **?** | No dedicated bytes |
| `layer/hit_material.cpp:954` basis capture jobs/status | `triangles*3*stride` <=**16** per job +64-byte status rounded64 KiB | On-demand; <=eight new jobs/frame (`common/native_hit_pool.h:121`); free two frames after resolve `:626` | Need/job; status BAR | Basis completion/caps, no full transient ledger |
| `layer/hit_material.cpp:314` idle recycle storage | Sum retired eligible buffers, <=**512** target | Receives retired streams; fit reuse; expiry 120 frames/over-budget eviction | Shared pool; **do not add both idle and active ownership of same buffer** | `[hit material streams]` recycler |

### Opacity micromaps (normally off in Ultra)

| Source / consumer | Payload formula; MiB A = B | Allocate; growth; shrink/free | Need/reserve; multiplicity | Additional log |
|---|---|---|---|---|
| `layer/omm_content.cpp:76-92` content epochs | `(65536+1)*max(16,minStorageBufferOffsetAlignment)`, bounded 64 MiB; **0** fresh Ultra; driver dependent if enabled | Initialization gated by OMM request (`layer/omm_backend.cpp:91,140,158`); cleanup at teardown | Fixed registry; BAR preferred, host fallback | `[omm content]` slots/stride/bytes/device_local |
| `layer/omm_bake.cpp:121,160-169` plain vertices, micromap inputs, triangle descriptors, audit, counters | Requested vertex bytes +`256+4*T` +`T*sizeof(Triangle)` +`16*T` +16-byte shared GPU stats +16 host stats; **0** fresh Ultra | On-demand tickets; global input budget **128**; shared-owner destruction after safe release; failed recorded work may retain until idle teardown (`:224`) | Need per ticket; host triangle/stat buffers conditional VRAM | OMM input/live-byte diagnostics |
| `layer/omm_backend.cpp:209-210` micromap backing/scratch | Driver `micromapSize + max(buildScratchSize,1)+alignment-1`; **0** fresh Ultra | Per ticket; combined backend budget **256**; release only after owner and recorded/submitted references retire (`:303-320`) | Need per ticket; scratch retained with object | `[omm] EXT build`; backend live bytes |
| `layer/omm_backend.cpp:275-276` OMM BLAS backing/scratch | Driver `ASsize + max(buildScratchSize,1)+alignment-1`; **0** fresh Ultra | Same resource lifecycle/budget; not extra 256 MiB budget | Need per ticket | OMM BLAS logs |

Geometry pool overhead is opaque: descriptor/query/command creation sites include
`layer/alphatest.cpp:628,1431,1590`, `layer/bvhsplit.cpp:944`,
`layer/emissive.cpp:1093,1120,1135`, `layer/geocapture.cpp:478,1196,1630`,
`layer/omm_content.cpp:100`, `layer/rtscene.cpp:768`, `layer/sun_scene.cpp:305`,
and `layer/water.cpp:251,271`. These are driver objects, not additional known-size VkBuffers.

The non-RT diagnostic capture also uses `layer/geocapture.cpp:451-467`: default
`geo_xfb_mb=768` plus `4*max(geo_max_draws,256)` counters. `g_cap` is readback/host-cached;
`g_scratch` can be a device-memory cost capture. These are alternate capture modes, not a
second 768 MiB allocation to add automatically to Ultra's 1024 MiB RT scratch. Copy buffers
at `:425` use `4*w*h`, and per-capture UBO copies at `:2553,2559` use 256 bytes.

## Evidence from real queue logs

Paths below are relative to **`C:/Users/quali/Documents/Code Projects/BG3RTX/captures/queue/logs/`**
in the main checkout. These are observed peaks/high-water examples in selected logged windows,
not a claim that every historical frame or the current playing session was captured.
Historical experimental settings differ from today's Ultra. Demand is not allocation capacity.

| Log file:line | Observed demand / capacity | What it establishes |
|---|---|---|
| `sw2-camp-fg2.1.213409.log:5401` | Alpha peak **404.6**, opaque peak **153.0**, RT buffer **1024** | Alpha peak exceeds its nominal 300 MiB region here; do not reduce the region solely from occupancy. The peaks are separate maxima, not necessarily simultaneous. |
| `m4i-instanced-s14-fg2.1.074646.log:1706` | Opaque **463.8**, alpha **223.8**, buffer **1408** | Other scene/settings need materially more opaque space; no universal trim can be justified from S8 alone. |
| `ls1-long-session-camp-fg2.1.084541.log:39507` | Cascade demand maximum **306.77 / 427** | Real lower-bound required capacity in this window; three buffers multiply reserve cost. |
| `su1-ultra-s8-fg2.1.041708.log:6801` | Cascade demand **207.35 / 427** | S8 Ultra has substantial reserve in this window, not proof the long view fits it. |
| `su1-ultra-s8-fg2.1.041708.log:6814` | Mesh pool **1255 / 2047**; live AS **267.1** in **29 chunks** | Mesh slack **792**; AS committed **>=1856**, slack **>=1588.9** including fragmentation. |
| `ls1-long-session-camp-fg2.1.084541.log:39858` | Mesh pool **341.5 / 1024**; live AS **255.4** in **66 chunks** | Mesh slack **682.5**; AS committed **>=4224**, slack **>=3968.6**. The AS log omits free committed chunk space. |
| `hig1-ip-grass-camp-fg2.1.102740.log:4064-4065` | Source demand **244.1 / 384**, deferred **266.2** in **9/13 chunks**, query basis **488.2 / 768**; actual streams **3296**, source **2144** in eight slots, deferred **1152** in four packets | Large historical duplication is real; this predates source trimming and enables experimental IP grass. **Not** today's guaranteed saving. |
| `hig1-ip-grass-camp-fg2.1.102740.log:4174` | Actual streams **1008**: source 512, deferred 256, basis 240 in two queries | Streaming overlap varies strongly; demand lines alone miss resident multiplicity. |
| `ls1-long-session-camp-fg2.1.084541.log:39837` | Source **51.0 / 128**, deferred **51.4** in two chunks, query basis **102.0 / 256**, deferred rows 80/4096 | Smaller scene after a larger scene; trim/recycle policy matters more than row metadata. |
| `ef1-af-em-fg2.1.020940.log:2809,2848` | Emission soup grows to **512** for **67.1/frame** demand and remains 512 | Four-demand target needs 268.4, but power-of-two growth rounds to 512. |
| `m4p5-s6-gidiag.1.014357.log:2723` | Emission soup grows 512 for **69.6/frame** | Another demand observation requiring 278.4 at the existing four-frame headroom policy. |
| `m5w2-water-reflections-s8-fg2.1.140754.log:1313` | Water **1,247,232 triangles / 120 frames**, about 10,394/frame | Geometry count observation; cannot translate to exact soup bytes without vertex stride/alignment for the draws. |
| `ls1-long-session-camp-fg2.1.084541.log:39788` | Water **139 triangles/build**, soup **32** | Very small demand with the same fixed minimum reserve; no observed growth to 256 in this evidence. |
| `su1-ultra-s8-fg2.1.041708.log:957` | Compose **5,242,878 records**, **112 bytes**, logged **559 MB** | Requested budget 560 MiB, integer payload truncates to 559 in log. Historical 104-byte/519 MB records are a different feature configuration. |

The user's reported BLAS growth 0.25 to 1.06 GB must therefore be checked for whether it
means live AS bytes or actual chunk allocations. Do not add that growth to `64*chunkCount`
as a second consumer. The reported cascade increase 427 to 544 costs **351 MiB across three
slots**, not 117 MiB total.

## Ten savings candidates, ranked

These are proposed changes, **not implemented or validated**. Ranking uses
`estimated reclaimable MiB * confidence / risk`, with confidence in 0..1 and risk 1..4
(1 simple retirement, 4 layout/patching change). The numbers are triage estimates, not measured
quality gates or probabilities. Ranking uses A and the stated scenario, not simultaneous guaranteed
savings. None of the figures may be summed across incompatible scenes or overlapping resources.
Reclamation always needs proof that recorded commands, submitted GPU work, descriptors and SDK
leases no longer reference the resource. No live test is authorized by this audit.

| Rank / score | Exact change and source | Potential A / B MiB saved | Confidence / risk; limitation |
|---|---|---|---|
| **1 / 794** | At `layer/bvhsplit.cpp:266,2565,995`, track safe retired occupancy per AS chunk and release wholly empty chunks; if fragmentation dominates, relocate/rebuild live BLAS into fewer chunks and update dependent TLAS before retiring old chunks | **Up to 3968.6 / same** slack in the 66-chunk example; actual empty-chunk saving unknown | **0.6 / 3**. Largest observed hidden reserve. Compaction slack is not automatically reclaimable; relocation temporarily costs extra memory. Chunk identifiers/free lists must survive removal. |
| **2 / 356** | At `layer/bvhsplit.cpp:337`, add GPU-safe mesh-pool shrink/repack after sustained scene demand decreases; use demand plus derived headroom rather than retaining the doubled high-water forever | **Up to 792 / same** S8 slack; less with required headroom;682.5 in camp | **0.9 / 2**. Pool offsets/AS dependencies must be preserved or rebuilt. Does not shrink live geometry quality. |
| **3 / 333** | At `layer/geocapture.cpp:1137,1424`, reset grown cascade reserve after a scene transition/idle demand interval and replace all three retired slots, retaining overflow-driven regrowth | **351 / same** when 544 can safely return to 427 | **0.95 / 1** for bytes, conditional on demand. Current long-view demand must be verified; observed 306.77 in another scene does not prove it fits. |
| **4 / 230** | At `layer/hit_material.cpp:314` and recycler policy `common/native_hit_recycle.h:23`, evict idle recycled allocations promptly under pressure instead of retaining up to 512 MiB/120 frames; leave active streams intact | **0..512 / same** | **0.9 / 2**. This is idle capacity, not a guaranteed512 allocation. Existing source trim must remain; do not claim historical 3296 MiB streams are all still retained. |
| **5 / 167** | At `core/sharc.cpp:280,106`, call deferred resource retirement when SHaRC is disabled; similarly retire disabled experimental auxiliaries at `core/gi.cpp:567`, `core/water.cpp:519,1269` | SHaRC **160..176 / same**; auxiliaries separately up to **83.36 / 112.50** | **0.95 / 1**, ranking uses176 only. **Zero in fresh Ultra**, useful after experiments; do not add auxiliary figures if never enabled. |
| **6 / 101** | At `layer/emissive.cpp:1431-1440`, replace doubling with aligned demand+headroom sizing and shrink after sustained lower demand; retire old generations by completion rather than 16 additional builds | Example 512 to about **288**, saving **224 / same**; 512 to 256 would save 256 only if four-demand headroom policy is reduced | **0.9 / 2**. Observed 69.6*4=278.4 means 256 is **not** sufficient under the current policy. Cost of more frequent allocation must be measured. |
| **7 / 43** | At `layer/geocapture.cpp:451-467`, support budget/demand-sized RT XFB at startup or a safe repatch/rebuild point, preserving separate alpha/opaque headroom and overflow recovery | Illustrative 1024 to 768 => **256 / same** | **0.5 / 3**. Historical separate peaks558 MiB suggest room, but mixed scenes and opaque 463.8 make a blanket cut unsafe. This is a sizing candidate, not a recommended immediate INI value. |
| **8 / 27** | At `layer/water.cpp:471-514`, replace baked fixed compose-buffer capacity with safely versioned/repatched resolution/demand capacity; preserve fragment overdraw headroom | If one-record-per-render-pixel sufficient:560 minus112*R => **268.24 / 166.25** | **0.4 / 4**. Fragment records can reflect overdraw; pixel count alone is **not** a safe cap. Address is baked in fragment shaders; allocation shrink needs pipeline/resource lifetime changes. |
| **9 / 21** | At `layer/streamline.cpp:668,693-738`, retire surplus unused MV/depth copy slots after SDK input-consumed fences, or bound slots by observed simultaneous leases with a safe fallback | Eight to two slots => **125.04 / 168.75** | **0.5 / 3**. Actual allocated/required slots not established; do not share live SDK inputs or assume two is enough. Already allocated lazily. |
| **10 / 19** | At `core/gi.cpp:554-559,2804,3161`, bypass temporal copy when RR uses no old history, rebind composite to raw, and omit two GI history/depth pairs only on that path | **62.52 / 84.38** | **0.9 / 3**. Requires binding/debug/RR-transition handling; deleting allocations alone breaks writes/reads. Temporal histories are not frame-in-flight duplicates. |

Other smaller cleanups include retiring the RR check image after diagnostics
(20.84 / 28.13), disabling unused water resources after keys are lowered (72.94 / 98.44),
and freeing VT-mask upload staging after its upload completes (catalogue bytes unknown;
VRAM saving only if staging is actually device-local). These are conditional savings, not
explanations of the user's current Ultra growth.

## Remaining uncertainty and verification boundary

This is a complete source-site inventory of explicit owned device-local buffer/image/AS allocation
families found by searching creation, memory allocation, helper callers and core host allocation
calls. Conditional host-visible buffers and opaque pools/SDKs are included rather than silently
assigned zero. It is **not** a complete runtime heap ledger: exact driver AS/image/pool sizes,
chunk occupancy, active query/packet overlap, game memory and SDK private allocations are unavailable
from source and selected capacity logs. Consequently a defensible single Ultra total or exact
attribution of the full 22 to 31.7 GB increase cannot be supplied from these inputs.

Next verification, when separately authorized, should reconcile existing log capacities with
committed chunk sizes and safe live-slot counts before choosing reserve reductions. No new
measurement tool is required to implement the known empty-chunk retirement fix; preserve the
other agent's layer budget-guard ownership. Shader request delta for this documentation change: **0**.
