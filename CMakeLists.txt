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
set(BG3RTX_BIN ${CMAKE_SOURCE_DIR}/bin)
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
  layer/geocapture.cpp
  layer/rtscene.cpp
  layer/alphatest.cpp
  layer/bvhsplit.cpp
  layer/streamline.cpp
  layer/savefilter.cpp
  layer/background.cpp
  layer/assetdb.cpp
  layer/hookguard.cpp
  layer/framecap.cpp
  layer/layer.def)
target_include_directories(VkLayer_bg3rtx PRIVATE ${VULKAN_SDK}/Include)

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

# Player settings and launcher: bin/BG3RTX.exe (tools/launcher, docs/USER_SETTINGS.md).
add_executable(bg3rtx_launcher WIN32 tools/launcher/main.cpp tools/launcher/launch.cpp tools/launcher/gpu.cpp)
set_target_properties(bg3rtx_launcher PROPERTIES OUTPUT_NAME BG3RTX)
target_include_directories(bg3rtx_launcher PRIVATE ${VULKAN_SDK}/Include)
target_compile_definitions(bg3rtx_launcher PRIVATE UNICODE _UNICODE)
target_link_libraries(bg3rtx_launcher PRIVATE comctl32 ole32 shell32 advapi32 user32 gdi32)

# Settings model unit tests (common/settings.h): bin/settings_test.exe, also via ctest.
enable_testing()
add_executable(settings_test tests/settings_test.cpp)
target_compile_definitions(settings_test PRIVATE BG3RTX_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME settings_test COMMAND settings_test)
# Asset database loader (layer/assetdb.cpp) against data/assetdb; skips when not built.
add_executable(assetdb_test tests/assetdb_test.cpp layer/assetdb.cpp)
target_include_directories(assetdb_test PRIVATE ${VULKAN_SDK}/Include)
target_compile_definitions(assetdb_test PRIVATE BG3RTX_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME assetdb_test COMMAND assetdb_test)

# Hot-reloadable core: rebuild while the game runs.
add_library(bg3rtx_core SHARED core/core.cpp core/gi.cpp)
target_include_directories(bg3rtx_core PRIVATE ${VULKAN_SDK}/Include)
# A fixed PDB name would stay locked by a debugger attached to the old copy.
set_target_properties(bg3rtx_core PROPERTIES PDB_NAME "bg3rtx_core_$<CONFIG>")
