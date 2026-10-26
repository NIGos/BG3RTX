// Inline-hook guard for the layer's own entry points.
//
// Some mods patch the code of whatever vkGetDeviceProcAddr returns. With this
// layer active that is *our* code: the BG3 Script Extender (bg3se) resolves
// vkQueuePresentKHR, vkCreateSwapchainKHR and vkDestroySwapchainKHR right
// after vkCreateDevice and Detours-patches their first bytes
// (BG3Extender/Extender/Client/IMGUI/Vulkan.inl, vkCreateDeviceHooked). Its
// present pre-hook then runs an ImGui pass on queue family 0 queue 0 and
// rewrites the present's wait semaphore before our present (and Streamline's
// DLSS-G swapchain) ever sees it. See docs/TRACK_B_FG.md, "Script Extender".
//
// This file snapshots the first bytes of chosen entry points before anyone
// can patch them, recognises a later inline patch, follows its jumps to the
// module that owns the detour, and can write the original bytes back.
// It depends on Windows only, so tools/hookguard_test.cpp tests it alone.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace bg3::guard {

constexpr size_t kSnap = 32;   // Detours rewrites 5 bytes plus the rest of the last instruction it splits

struct Entry {
  const char* name = nullptr;
  uint8_t* code = nullptr;      // first instruction of the function body (incremental-link thunks followed)
  uint8_t orig[kSnap] = {};
  bool armed = false;
};

struct Patch {
  bool patched = false;
  size_t changed = 0;              // bytes from the start up to the last one that differs
  const void* first = nullptr;     // where the patched entry jumps
  const void* final = nullptr;     // end of the jump chain (the detour function)
  std::wstring module;             // file name of the module that owns `final` ("" when none)
};

// Decodes one unconditional jump at p (jmp rel8/rel32, jmp [rip+disp32],
// mov rax/r11, imm64 + jmp reg). Returns its target, or null if p is not one
// or is not readable.
const void* follow_jump(const void* p);

// Module file name (no directory) that contains p, "" for none.
std::wstring module_of(const void* p);

// Records the original bytes. Jump thunks are followed only while they stay
// inside fn's own module, so a thunk can never be confused with a patch.
bool snapshot(Entry& e, const char* name, const void* fn);

Patch inspect(const Entry& e);

// Writes the snapshot back over the changed bytes. Callers must make sure no
// thread is executing those bytes (see layer.cpp, guard_check).
bool restore(Entry& e, const Patch& p);

// Case-insensitive substring test on a module name.
bool module_matches(const std::wstring& module, const wchar_t* needle);

}  // namespace bg3::guard
