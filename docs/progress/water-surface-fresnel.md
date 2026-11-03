## Water interface / column split (branch water-shader-analysis, READY_FOR_LIVE, 2026-11-03)
- rt_water_surface_fresnel defaults 0 in every preset; visible Surfaces control, restart to first enable, live toggles after provisioning.
- Pool FS c4e92ccdb08dd777, foam SSA 751 == 0 only; unsupported families and foam pixels retain native shading.
- SpecId 1025; interface A/B independent of opacity, column weighted by 1-F; no reflection trace/hit shade edits or record growth.
- Build PASS; 144/144 CTests pass (166.11 s), all 7 candidate FS forms valid; CPU 712,551 checks incl. 5,544 partitions, settings 4,200 pass.
- Shader gate PASS: 2/2 raw ordinary requests identical; 16/16 compose arithmetic-OFF identical to HEAD, 16/16 production valid, no missing requests; capability raw modules change.
- shaders/variants.txt request delta 0; default FS patch words unchanged. No commits or Git metadata writes.
- Prepared S8 Ultra pool-rim static/Q/E same-run A/B: lint + fake dryrun pass, one launch, 4.3 minutes, estimated 490 MB; not queued/run.
- GPU compose cost unmeasured; inspect shallow rim reflections and post-water float colour against reference partition; no equal-mean gate or quality pass.
