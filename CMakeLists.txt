cmake_minimum_required(VERSION 3.25)
project(BG3RTX LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT DEFINED ENV{VULKAN_SDK})
  message(FATAL_ERROR "VULKAN_SDK is not set")
endif()
file(TO_CMAKE_PATH "$ENV{VULKAN_SDK}" VULKAN_SDK)

# Everything lands in bin/ so the layer runs straight from the repository.
# A second build tree (for example BG3RTX_DIAGNOSTICS=OFF) can point it elsewhere.
set(BG3RTX_BIN ${CMAKE_SOURCE_DIR}/bin CACHE PATH "Output folder of the DLLs and executables")
foreach(cfg "" _DEBUG _RELEASE _RELWITHDEBINFO)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY${cfg} ${BG3RTX_BIN})
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY${cfg} ${CMAKE_BINARY_DIR}/lib)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY${cfg} ${CMAKE_BINARY_DIR}/lib)
endforeach()

add_compile_definitions(VK_NO_PROTOTYPES VK_USE_PLATFORM_WIN32_KHR NOMINMAX WIN32_LEAN_AND_MEAN)
add_compile_options(/W3 /utf-8 /Zi)
add_link_options(/DEBUG)

# Stable host: the Vulkan layer. Restart the game to pick up changes here.
add_library(VkLayer_bg3rtx SHARED
  layer/layer.cpp
  layer/tracker.cpp
  layer/cmd.cpp
  layer/tracer.cpp
  layer/hotreload.cpp
  layer/shaders.cpp
  layer/util.cpp
  layer/dump.cpp
  layer/ngx.cpp
  layer/rrpost.cpp
  layer/geocapture.cpp
  layer/rtscene.cpp
  layer/sun_scene.cpp
  layer/omm_backend.cpp
  layer/omm_submit.cpp
  layer/omm_content.cpp
  layer/omm_sampler.cpp
  layer/omm_native_plain.cpp
  layer/omm_bake.cpp
  layer/alphatest.cpp
  layer/hit_material.cpp
  layer/bvhsplit.cpp
  layer/emissive.cpp
  layer/devfault.cpp
  layer/aftermath.cpp
  layer/streamline.cpp
  layer/savefilter.cpp
  layer/background.cpp
  layer/assetdb.cpp
  layer/sebridge.cpp
  layer/hookguard.cpp
  layer/framecap.cpp
  layer/layer.def)
target_include_directories(VkLayer_bg3rtx PRIVATE ${VULKAN_SDK}/Include)
# Diagnostic-only host code (terrain packet dump, pan bursts, rt_order_trace, cpu_timing
# hook timers, ls_native_direct_validate). OFF compiles it out; the hooks become no-ops.
option(BG3RTX_DIAGNOSTICS "Compile diagnostic-only host code into the layer" ON)
target_compile_definitions(VkLayer_bg3rtx PRIVATE BG3RTX_DIAGNOSTICS=$<BOOL:${BG3RTX_DIAGNOSTICS}>)

# NVIDIA Nsight Aftermath SDK (optional, aftermath=1 in bg3rtx.ini): headers at build
# time; GFSDK_Aftermath_Lib.x64.dll is copied into bin/ for development and loaded at
# run time (scripts/package.ps1 copies an explicit file list, so it is never packaged).
set(BG3RTX_AFTERMATH_SDK "D:/BG3_Tools/nsight-aftermath-sdk-2026.3" CACHE PATH "Nsight Aftermath SDK (include/, lib/x64/)")
if(EXISTS "${BG3RTX_AFTERMATH_SDK}/include/GFSDK_Aftermath.h")
  message(STATUS "Nsight Aftermath SDK: ${BG3RTX_AFTERMATH_SDK}")
  target_include_directories(VkLayer_bg3rtx PRIVATE ${BG3RTX_AFTERMATH_SDK}/include)
  target_compile_definitions(VkLayer_bg3rtx PRIVATE BG3RTX_AFTERMATH=1)
  add_custom_command(TARGET VkLayer_bg3rtx POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${BG3RTX_AFTERMATH_SDK}/lib/x64/GFSDK_Aftermath_Lib.x64.dll
            ${BG3RTX_BIN}/GFSDK_Aftermath_Lib.x64.dll)
  add_executable(aftermath_decode tools/aftermath_decode.cpp)
  target_include_directories(aftermath_decode PRIVATE ${BG3RTX_AFTERMATH_SDK}/include ${VULKAN_SDK}/Include)
  target_link_libraries(aftermath_decode PRIVATE ${BG3RTX_AFTERMATH_SDK}/lib/x64/GFSDK_Aftermath_Lib.x64.lib)
else()
  message(STATUS "Nsight Aftermath SDK not found (BG3RTX_AFTERMATH_SDK): aftermath=1 is unavailable")
endif()

# NVIDIA SDKs (DLSS headers only: NGX is reached through the driver's
# _nvngx.dll at run time, nothing is linked). external/ is not versioned; a
# worktree under .claude/worktrees/ finds the main checkout's copy.
set(BG3RTX_EXTERNAL "${CMAKE_SOURCE_DIR}/external" CACHE PATH "NVIDIA SDK clones (DLSS, Streamline, NRD, SHARC)")
if(NOT EXISTS "${BG3RTX_EXTERNAL}/DLSS/include/nvsdk_ngx_vk.h")
  get_filename_component(_main "${CMAKE_SOURCE_DIR}/../../../external" ABSOLUTE)
  if(EXISTS "${_main}/DLSS/include/nvsdk_ngx_vk.h")
    set(BG3RTX_EXTERNAL "${_main}" CACHE PATH "" FORCE)
  else()
    message(FATAL_ERROR "DLSS SDK not found: clone github.com/NVIDIA/DLSS into ${BG3RTX_EXTERNAL}/DLSS")
  endif()
endif()
target_include_directories(VkLayer_bg3rtx PRIVATE ${BG3RTX_EXTERNAL}/DLSS/include)

# NVIDIA Streamline (DLSS-G): headers at build time, prebuilt signed binaries
# copied next to the layer (bin/sl production, bin/sl_dev development).
# external/ is not part of the repository; a worktree finds the main checkout's.
set(BG3RTX_SL_SDK "" CACHE PATH "Streamline SDK release (with bin/x64 and include/)")
if(NOT BG3RTX_SL_SDK)
  foreach(c "${CMAKE_SOURCE_DIR}/external/streamline-sdk-v2.14.16"
            "$ENV{BG3RTX_SL_SDK}"
            "${CMAKE_SOURCE_DIR}/../../../external/streamline-sdk-v2.14.16")
    if(c AND EXISTS "${c}/include/sl.h")
      get_filename_component(BG3RTX_SL_SDK "${c}" ABSOLUTE)
      break()
    endif()
  endforeach()
endif()
if(NOT BG3RTX_SL_SDK)
  message(FATAL_ERROR "Streamline SDK not found: download streamline-sdk-v2.14.16.zip from github.com/NVIDIA-RTX/Streamline/releases into external/streamline-sdk-v2.14.16 or set BG3RTX_SL_SDK")
endif()
message(STATUS "Streamline SDK: ${BG3RTX_SL_SDK}")
target_include_directories(VkLayer_bg3rtx PRIVATE ${BG3RTX_SL_SDK}/include)
# nvngx_dlss(d) are there for the game: its DLSS SR shares Streamline's NGX
# instance (fg_game_ngx=2), which looks for snippets in this folder.
set(SL_DLLS sl.interposer.dll sl.common.dll sl.dlss_g.dll sl.reflex.dll sl.pcl.dll nvngx_dlssg.dll NvLowLatencyVk.dll
    nvngx_dlss.dll nvngx_dlssd.dll)
foreach(dll ${SL_DLLS})
  add_custom_command(TARGET VkLayer_bg3rtx POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BG3RTX_BIN}/sl ${BG3RTX_BIN}/sl_dev
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${BG3RTX_SL_SDK}/bin/x64/${dll} ${BG3RTX_BIN}/sl/${dll}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${BG3RTX_SL_SDK}/bin/x64/development/${dll} ${BG3RTX_BIN}/sl_dev/${dll})
endforeach()
add_custom_command(TARGET VkLayer_bg3rtx POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${BG3RTX_SL_SDK}/bin/x64/development/WinPixEventRuntime.dll ${BG3RTX_BIN}/sl_dev/WinPixEventRuntime.dll)
add_custom_command(TARGET VkLayer_bg3rtx POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/layer/VkLayer_bg3rtx.json ${BG3RTX_BIN}/VkLayer_bg3rtx.json)

# Unit test of the inline-hook guard (layer/hookguard.cpp): mock Detours-style patches.
add_executable(hookguard_test tools/hookguard_test.cpp layer/hookguard.cpp)

# Track D tools: offline check of the XFB shader patch.
add_executable(xfb_patch_test tools/xfb_patch_test.cpp)
# Track D cost probe: BLAS/TLAS builds over a captured scene.bin on its own device.
add_executable(geo_blas_bench tools/geo_blas_bench.cpp)
target_include_directories(geo_blas_bench PRIVATE ${VULKAN_SDK}/Include)
target_link_directories(geo_blas_bench PRIVATE ${VULKAN_SDK}/Lib)
# Driver statistics (registers) of a compute shader on its own device: tools/pipeline_stats.py.
add_executable(pipeline_stats tools/pipeline_stats.cpp)
target_include_directories(pipeline_stats PRIVATE ${VULKAN_SDK}/Include)
target_link_directories(pipeline_stats PRIVATE ${VULKAN_SDK}/Lib)

# Player settings and launcher: bin/BG3RTX.exe (tools/launcher, docs/USER_SETTINGS.md).
add_executable(bg3rtx_launcher WIN32 tools/launcher/main.cpp tools/launcher/launch.cpp tools/launcher/gpu.cpp)
set_target_properties(bg3rtx_launcher PROPERTIES OUTPUT_NAME BG3RTX)
target_include_directories(bg3rtx_launcher PRIVATE ${VULKAN_SDK}/Include)
target_compile_definitions(bg3rtx_launcher PRIVATE UNICODE _UNICODE)
target_link_libraries(bg3rtx_launcher PRIVATE comctl32 ole32 shell32 advapi32 user32 gdi32)

# Settings model unit tests (common/settings.h): bin/settings_test.exe, also via ctest.
enable_testing()
add_executable(native_hit_gi_bindings_test tests/native_hit_gi_bindings_test.cpp)
target_include_directories(native_hit_gi_bindings_test PRIVATE ${VULKAN_SDK}/Include)
add_test(NAME native_hit_gi_bindings_test COMMAND native_hit_gi_bindings_test)
add_executable(native_hit_rf_bindings_test tests/native_hit_rf_bindings_test.cpp)
target_include_directories(native_hit_rf_bindings_test PRIVATE ${VULKAN_SDK}/Include)
add_test(NAME native_hit_rf_bindings_test COMMAND native_hit_rf_bindings_test)
add_executable(native_hit_pipeline_test tests/native_hit_pipeline_test.cpp)
target_include_directories(native_hit_pipeline_test PRIVATE ${VULKAN_SDK}/Include)
add_test(NAME native_hit_pipeline_test COMMAND native_hit_pipeline_test)
add_executable(native_hit_tileset_inputs_test tests/native_hit_tileset_inputs_test.cpp)
target_include_directories(native_hit_tileset_inputs_test PRIVATE ${VULKAN_SDK}/Include)
add_test(NAME native_hit_tileset_inputs_test COMMAND native_hit_tileset_inputs_test)
add_executable(native_hit_order_test tests/native_hit_order_test.cpp)
add_test(NAME native_hit_order_test COMMAND native_hit_order_test)
add_executable(native_hit_join_test tests/native_hit_join_test.cpp)
add_test(NAME native_hit_join_test COMMAND native_hit_join_test)
add_executable(native_hit_abi_test tests/native_hit_abi_test.cpp)
target_include_directories(native_hit_abi_test PRIVATE ${VULKAN_SDK}/Include)
add_test(NAME native_hit_abi_test COMMAND native_hit_abi_test)
add_executable(material_submit_stages_test tests/material_submit_stages_test.cpp)
target_include_directories(material_submit_stages_test PRIVATE ${VULKAN_SDK}/Include)
add_test(NAME material_submit_stages_test COMMAND material_submit_stages_test)
add_executable(transport_bsdf_test tests/transport_bsdf_test.cpp)
add_test(NAME transport_bsdf_test COMMAND transport_bsdf_test)
add_executable(native_hit_bounce_test tests/native_hit_bounce_test.cpp)
add_test(NAME native_hit_bounce_test COMMAND native_hit_bounce_test)
add_executable(native_hit_direct_test tests/native_hit_direct_test.cpp)
add_test(NAME native_hit_direct_test COMMAND native_hit_direct_test)
add_executable(terrain_packet_contract_test tests/terrain_packet_contract_test.cpp)
add_test(NAME terrain_packet_contract_test COMMAND terrain_packet_contract_test)
add_executable(native_terrain_surface_test tests/native_terrain_surface_test.cpp)
add_test(NAME native_terrain_surface_test COMMAND native_terrain_surface_test)
add_executable(transport_frame_test tests/transport_frame_test.cpp)
add_test(NAME transport_frame_test COMMAND transport_frame_test)
add_executable(native_opaque_transport_test tests/native_opaque_transport_test.cpp)
add_test(NAME native_opaque_transport_test COMMAND native_opaque_transport_test)
add_executable(omm_scene_binding_test tests/omm_scene_binding_test.cpp)
add_test(NAME omm_scene_binding_test COMMAND omm_scene_binding_test)
add_executable(native_tileset_surface_test tests/native_tileset_surface_test.cpp)
add_test(NAME native_tileset_surface_test COMMAND native_tileset_surface_test)
add_executable(native_vt_surface_test tests/native_vt_surface_test.cpp)
add_test(NAME native_vt_surface_test COMMAND native_vt_surface_test)
add_executable(trace_sampled_view_test tests/trace_sampled_view_test.cpp)
target_include_directories(trace_sampled_view_test PRIVATE ${VULKAN_SDK}/Include)
add_test(NAME trace_sampled_view_test COMMAND trace_sampled_view_test)
add_executable(trace_terrain_table_test tests/trace_terrain_table_test.cpp)
add_test(NAME trace_terrain_table_test COMMAND trace_terrain_table_test)
add_executable(omm_material_cache_test tests/omm_material_cache_test.cpp)
add_test(NAME omm_material_cache_test COMMAND omm_material_cache_test)
add_executable(omm_spatial_support_test tests/omm_spatial_support_test.cpp)
add_test(NAME omm_spatial_support_test COMMAND omm_spatial_support_test)
add_executable(omm_plain_bake_test tests/omm_plain_bake_test.cpp)
add_test(NAME omm_plain_bake_test COMMAND omm_plain_bake_test)
add_executable(sun_raster_domain_test tests/sun_raster_domain_test.cpp)
target_include_directories(sun_raster_domain_test PRIVATE ${VULKAN_SDK}/Include)
add_test(NAME sun_raster_domain_test COMMAND sun_raster_domain_test)
add_executable(sun_caster_coverage_test tests/sun_caster_coverage_test.cpp)
add_test(NAME sun_caster_coverage_test COMMAND sun_caster_coverage_test)
add_executable(native_hair_geometry_test tests/native_hair_geometry_test.cpp)
add_test(NAME native_hair_geometry_test COMMAND native_hair_geometry_test)
add_executable(native_hair_origin_test tests/native_hair_origin_test.cpp)
add_test(NAME native_hair_origin_test COMMAND native_hair_origin_test)
add_executable(shader_variant_test tests/shader_variant_test.cpp)
add_test(NAME shader_variant_test COMMAND shader_variant_test ${CMAKE_SOURCE_DIR})
add_executable(native_hair_receiver_test tests/native_hair_receiver_test.cpp)
add_test(NAME native_hair_receiver_test COMMAND native_hair_receiver_test)
add_executable(native_hair_test tests/native_hair_test.cpp)
add_test(NAME native_hair_test COMMAND native_hair_test)
add_executable(rt_capture_budget_test tests/rt_capture_budget_test.cpp)
add_test(NAME rt_capture_budget_test COMMAND rt_capture_budget_test)
add_executable(native_fur_test tests/native_fur_test.cpp)
add_test(NAME native_fur_test COMMAND native_fur_test)
add_executable(native_tileset_coverage_test tests/native_tileset_coverage_test.cpp)
add_test(NAME native_tileset_coverage_test COMMAND native_tileset_coverage_test)
add_executable(native_scalp_coverage_test tests/native_scalp_coverage_test.cpp)
add_test(NAME native_scalp_coverage_test COMMAND native_scalp_coverage_test)
add_executable(omm_classify_test tests/omm_classify_test.cpp)
add_test(NAME omm_classify_test COMMAND omm_classify_test)
add_executable(omm_alpha_bounds_test tests/omm_alpha_bounds_test.cpp)
add_test(NAME omm_alpha_bounds_test COMMAND omm_alpha_bounds_test)
add_executable(omm_bake_identity_test tests/omm_bake_identity_test.cpp)
add_test(NAME omm_bake_identity_test COMMAND omm_bake_identity_test)
# Optional OFFLINE CPU-baker probe. This does not enable a Vulkan extension or
# create a device; it verifies the installed SDK's incompatible strict cutoff.
option(BG3RTX_OMM_CPU_PROBE "Build the offline OMM SDK 1.10 CPU contract probe" OFF)
if(BG3RTX_OMM_CPU_PROBE)
  set(BG3RTX_OMM_SDK "${BG3RTX_EXTERNAL}/omm-sdk-1.10.0" CACHE PATH "Offline OMM SDK 1.10")
  if(NOT EXISTS "${BG3RTX_OMM_SDK}/include/omm.h" OR NOT EXISTS "${BG3RTX_OMM_SDK}/lib/omm-lib.lib"
      OR NOT EXISTS "${BG3RTX_OMM_SDK}/bin/omm-lib.dll")
    message(FATAL_ERROR "OMM CPU probe requested but BG3RTX_OMM_SDK is incomplete")
  endif()
  add_executable(omm_cpu_contract_probe tools/omm_cpu_contract_probe.cpp)
  target_include_directories(omm_cpu_contract_probe PRIVATE ${BG3RTX_OMM_SDK}/include)
  target_link_libraries(omm_cpu_contract_probe PRIVATE ${BG3RTX_OMM_SDK}/lib/omm-lib.lib)
  add_custom_command(TARGET omm_cpu_contract_probe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${BG3RTX_OMM_SDK}/bin/omm-lib.dll ${BG3RTX_BIN}/omm-lib.dll)
  add_test(NAME omm_cpu_contract_probe COMMAND omm_cpu_contract_probe)
endif()
add_executable(descriptor_epoch_test tests/descriptor_epoch_test.cpp)
add_test(NAME descriptor_epoch_test COMMAND descriptor_epoch_test)
add_executable(mapped_copy_test tests/mapped_copy_test.cpp)
add_test(NAME mapped_copy_test COMMAND mapped_copy_test)
add_executable(omm_backend_plan_test tests/omm_backend_plan_test.cpp)
add_test(NAME omm_backend_plan_test COMMAND omm_backend_plan_test)
add_executable(omm_submit_state_test tests/omm_submit_state_test.cpp)
add_test(NAME omm_submit_state_test COMMAND omm_submit_state_test)
add_executable(omm_content_registry_test tests/omm_content_registry_test.cpp)
add_test(NAME omm_content_registry_test COMMAND omm_content_registry_test)
add_executable(omm_native_plain_recipes_test tests/omm_native_plain_recipes_test.cpp)
add_test(NAME omm_native_plain_recipes_test COMMAND omm_native_plain_recipes_test)
add_executable(omm_sampler_metadata_test tests/omm_sampler_metadata_test.cpp)
target_include_directories(omm_sampler_metadata_test PRIVATE ${VULKAN_SDK}/Include)
add_test(NAME omm_sampler_metadata_test COMMAND omm_sampler_metadata_test)
add_executable(omm_sampler_bindings_test tests/omm_sampler_bindings_test.cpp layer/omm_sampler.cpp)
target_include_directories(omm_sampler_bindings_test PRIVATE ${VULKAN_SDK}/Include)
add_test(NAME omm_sampler_bindings_test COMMAND omm_sampler_bindings_test)
add_executable(omm_native_plain_test tests/omm_native_plain_test.cpp layer/omm_native_plain.cpp)
target_include_directories(omm_native_plain_test PRIVATE ${VULKAN_SDK}/Include)
add_test(NAME omm_native_plain_test COMMAND omm_native_plain_test)
add_executable(native_sun_math_test tests/native_sun_math_test.cpp)
add_test(NAME native_sun_math_test COMMAND native_sun_math_test)
# Explicit synthetic GPU probe only. Never registered in routine CTest and never
# launched by a build. No game, presentation or global loader settings changes.
option(BG3RTX_OMM_GPU_SMOKE "Build the opt-in synthetic EXT OMM console probe" OFF)
if(BG3RTX_OMM_GPU_SMOKE)
  add_executable(omm_ext_smoke tools/omm_ext_smoke.cpp layer/omm_backend.cpp layer/omm_submit.cpp layer/omm_bake.cpp layer/omm_content.cpp)
  target_include_directories(omm_ext_smoke PRIVATE ${VULKAN_SDK}/Include)
endif()
add_executable(spirv_visibility_test tests/spirv_visibility_test.cpp)
add_test(NAME spirv_visibility_test COMMAND spirv_visibility_test)
add_executable(settings_test tests/settings_test.cpp)
target_compile_definitions(settings_test PRIVATE BG3RTX_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME settings_test COMMAND settings_test)
# Asset database loader (layer/assetdb.cpp) against data/assetdb; skips when not built.
add_executable(se_classification_test tests/se_classification_test.cpp layer/sebridge.cpp)
target_include_directories(se_classification_test PRIVATE ${VULKAN_SDK}/Include)
add_test(NAME se_classification_test COMMAND se_classification_test)
add_executable(assetdb_test tests/assetdb_test.cpp layer/assetdb.cpp layer/sebridge.cpp)
target_include_directories(assetdb_test PRIVATE ${VULKAN_SDK}/Include)
target_compile_definitions(assetdb_test PRIVATE BG3RTX_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME assetdb_test COMMAND assetdb_test)
# Light BVH for many-light sampling (common/light_tree.h, docs/DESIGN_SOTA_NEXT.md item 1).
add_executable(light_tree_test tests/light_tree_test.cpp)
add_test(NAME light_tree_test COMMAND light_tree_test)
add_executable(pan_epoch_test tests/pan_epoch_test.cpp)
add_test(NAME pan_epoch_test COMMAND pan_epoch_test)
add_executable(camera_epoch_test tests/camera_epoch_test.cpp)
add_test(NAME camera_epoch_test COMMAND camera_epoch_test)
add_executable(camera_motion_test tests/camera_motion_test.cpp)
add_test(NAME camera_motion_test COMMAND camera_motion_test)
add_executable(dyn_hold_test tests/dyn_hold_test.cpp)
add_test(NAME dyn_hold_test COMMAND dyn_hold_test)
add_executable(fg_stability_test tests/fg_stability_test.cpp)
add_test(NAME fg_stability_test COMMAND fg_stability_test)
add_executable(fg_copy_lease_test tests/fg_copy_lease_test.cpp)
add_test(NAME fg_copy_lease_test COMMAND fg_copy_lease_test)
add_executable(async_capture_queue_test tests/async_capture_queue_test.cpp)
add_test(NAME async_capture_queue_test COMMAND async_capture_queue_test)
# rt_cascade_casters (common/cascade_casters.h, docs/TRACK_D_RT.md 6.7 item 2).
add_executable(cascade_casters_test tests/cascade_casters_test.cpp)
add_test(NAME cascade_casters_test COMMAND cascade_casters_test)
add_executable(native_light_validation_test tests/native_light_validation_test.cpp)
target_include_directories(native_light_validation_test PRIVATE ${VULKAN_SDK}/Include)
add_test(NAME native_light_validation_test COMMAND native_light_validation_test)
add_executable(ufloat_interval_test tests/ufloat_interval_test.cpp)
add_test(NAME ufloat_interval_test COMMAND ufloat_interval_test)
add_executable(native_sun_packets_test tests/native_sun_packets_test.cpp)
add_test(NAME native_sun_packets_test COMMAND native_sun_packets_test)
add_executable(native_hit_qualify_test tests/native_hit_qualify_test.cpp)
add_test(NAME native_hit_qualify_test COMMAND native_hit_qualify_test)
add_executable(emissive_policy_test tests/emissive_policy_test.cpp)
add_test(NAME emissive_policy_test COMMAND emissive_policy_test)
# D-EM policy in the core (ABI 24): core/em_policy.cpp against the former layer path, bit for bit.
add_executable(emissive_core_test tests/emissive_core_test.cpp core/em_policy.cpp)
target_include_directories(emissive_core_test PRIVATE ${VULKAN_SDK}/Include)
add_test(NAME emissive_core_test COMMAND emissive_core_test)

# Hot-reloadable core: rebuild while the game runs.
option(BG3RTX_HAIR_GPU_PROBE "Build the isolated, windowless native Hair Vulkan probe" OFF)
if(BG3RTX_HAIR_GPU_PROBE)
  add_executable(native_hair_gpu_probe tools/native_hair_gpu_probe.cpp)
  target_include_directories(native_hair_gpu_probe PRIVATE ${VULKAN_SDK}/Include)
  target_link_directories(native_hair_gpu_probe PRIVATE ${VULKAN_SDK}/Lib)
  target_link_libraries(native_hair_gpu_probe PRIVATE vulkan-1)
endif()

add_library(bg3rtx_core SHARED core/core.cpp core/gi.cpp core/ls_lights.cpp core/sun_trust.cpp core/sharc.cpp core/emissive.cpp core/em_policy.cpp)
target_include_directories(bg3rtx_core PRIVATE ${VULKAN_SDK}/Include)
# SHaRC radiance cache: inject the D-EM effects-target emission. AUTO follows whether
# common/host_api.h defines BG3RTX_HOST_HAS_RT_EMISSIVE (D-EM's Bg3HostApi::rt_emissive);
# ON forces it (needs D-EM), OFF builds without it.
set(BG3RTX_SHARC_EM "AUTO" CACHE STRING "SHaRC injection of the D-EM emission: AUTO, ON or OFF")
if(BG3RTX_SHARC_EM STREQUAL "OFF")
  target_compile_definitions(bg3rtx_core PRIVATE BG3RTX_SHARC_EM=0)
elseif(BG3RTX_SHARC_EM STREQUAL "ON")
  target_compile_definitions(bg3rtx_core PRIVATE BG3RTX_SHARC_EM=1)
endif()
# A fixed PDB name would stay locked by a debugger attached to the old copy.
set_target_properties(bg3rtx_core PROPERTIES PDB_NAME "bg3rtx_core_$<CONFIG>")
