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
add_custom_command(TARGET VkLayer_bg3rtx POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/layer/VkLayer_bg3rtx.json ${BG3RTX_BIN}/VkLayer_bg3rtx.json)

# Hot-reloadable core: rebuild while the game runs.
add_library(bg3rtx_core SHARED core/core.cpp core/gi.cpp)
target_include_directories(bg3rtx_core PRIVATE ${VULKAN_SDK}/Include)
# A fixed PDB name would stay locked by a debugger attached to the old copy.
set_target_properties(bg3rtx_core PROPERTIES PDB_NAME "bg3rtx_core_$<CONFIG>")
