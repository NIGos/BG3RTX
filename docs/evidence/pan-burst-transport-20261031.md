# Pan burst transport correction

R5's four optional vegetation bursts had 16 lighting frames but only 15 final
images. Their initial missing final frames were 3471, 6385, 9277 and 13087.
`valid_transport=true` checked copy/submission success without required-view
completeness. Depth armed capture after the separate tonemap recording could
already finish. The existing extra frame was conditional on `pan_inputs`, which
was correctly off for these K16 captures.

The old loader also required final to share the lighting command recording.
That contradicted the producer's documented separate native tonemap command
buffer. Every actual R5 G-buffer/RT/pose group shared its lighting recording;
the final's distinct recording was legitimate.

Future captures record 17 raw frames, explicitly omit the initial warmup, and
publish 16 consecutive retained frames. Manifest version 2 records K=16,
raw_record_frames=17 and omitted_warmup_record_frame. Missing required views,
duplicate required views, zero recordings or mixed lighting recordings invalidate
transport before publication. Final needs a valid recording and the same frame
index, but may come from its separate command buffer. The loader applies the
same grouping, rejects duplicate views and never labels 15 complete frames K16.

CPU checks cover distinct-final validity, mixed G-buffer/RT rejection and missing
first-final refusal. The existing C++ epoch test also checks explicit warmup
exclusion and required-view completeness. Twenty-one Python tests pass. Host DLL
and pan_epoch_test build and pass in the isolated checkout. No game/GPU test or
matrix rerun was performed; future runtime validation remains unmeasured.

No shader, ABI or user setting changed. Capture-off rendering is unchanged.
Diagnostic capture allocates one additional raw warmup frame (6.25% working-set
overhead); only the retained 16 frames are written. Existing per-burst 6 GiB cap
includes the warmup. No raw R5 files, reports or capacity-fix verdict were changed.
