// Inline-hook guard: see hookguard.h.
#include "hookguard.h"

#include <cstring>
#include <cwctype>

namespace bg3::guard {

namespace {

bool readable(const void* p, size_t n) {
  MEMORY_BASIC_INFORMATION mbi{};
  auto a = reinterpret_cast<uintptr_t>(p);
  while (n) {
    if (!VirtualQuery(reinterpret_cast<const void*>(a), &mbi, sizeof mbi)) return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    uintptr_t end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    size_t here = size_t(end - a) < n ? size_t(end - a) : n;
    a += here;
    n -= here;
  }
  return true;
}

HMODULE module_handle(const void* p) {
  HMODULE m = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          static_cast<LPCWSTR>(p), &m))
    return nullptr;
  return m;
}

}  // namespace

const void* follow_jump(const void* at) {
  auto p = static_cast<const uint8_t*>(at);
  if (!readable(p, 2)) return nullptr;
  if (p[0] == 0xE9 && readable(p, 5)) {
    int32_t rel;
    std::memcpy(&rel, p + 1, 4);
    return p + 5 + rel;
  }
  if (p[0] == 0xEB) return p + 2 + int8_t(p[1]);
  if (p[0] == 0xFF && p[1] == 0x25 && readable(p, 6)) {   // jmp [rip+disp32]
    int32_t disp;
    std::memcpy(&disp, p + 2, 4);
    const uint8_t* slot = p + 6 + disp;
    if (!readable(slot, sizeof(void*))) return nullptr;
    const void* t;
    std::memcpy(&t, slot, sizeof t);
    return t;
  }
  if (p[0] == 0x48 && p[1] == 0xB8 && readable(p, 12) && p[10] == 0xFF && p[11] == 0xE0) {   // mov rax, imm64; jmp rax
    const void* t;
    std::memcpy(&t, p + 2, sizeof t);
    return t;
  }
  if (p[0] == 0x49 && p[1] == 0xBB && readable(p, 13) && p[10] == 0x41 && p[11] == 0xFF && p[12] == 0xE3) {   // mov r11, imm64; jmp r11
    const void* t;
    std::memcpy(&t, p + 2, sizeof t);
    return t;
  }
  return nullptr;
}

std::wstring module_of(const void* p) {
  HMODULE m = p ? module_handle(p) : nullptr;
  if (!m) return L"";
  wchar_t path[MAX_PATH];
  DWORD n = GetModuleFileNameW(m, path, MAX_PATH);
  if (!n) return L"";
  std::wstring s(path, n);
  size_t slash = s.find_last_of(L"\\/");
  return slash == std::wstring::npos ? s : s.substr(slash + 1);
}

bool module_matches(const std::wstring& module, const wchar_t* needle) {
  std::wstring a = module, b = needle;
  for (auto& c : a) c = wchar_t(std::towlower(c));
  for (auto& c : b) c = wchar_t(std::towlower(c));
  return !b.empty() && a.find(b) != std::wstring::npos;
}

bool snapshot(Entry& e, const char* name, const void* fn) {
  e = Entry{};
  e.name = name;
  if (!fn) return false;
  HMODULE own = module_handle(fn);
  auto p = static_cast<const uint8_t*>(fn);
  for (int hop = 0; hop < 4; ++hop) {   // incremental-link / import thunks of our own module
    const void* t = follow_jump(p);
    if (!t || !own || module_handle(t) != own) break;
    p = static_cast<const uint8_t*>(t);
  }
  if (!readable(p, kSnap)) return false;
  e.code = const_cast<uint8_t*>(p);
  std::memcpy(e.orig, p, kSnap);
  e.armed = true;
  return true;
}

Patch inspect(const Entry& e) {
  Patch r;
  if (!e.armed || !readable(e.code, kSnap)) return r;
  for (size_t i = 0; i < kSnap; ++i)
    if (e.code[i] != e.orig[i]) r.changed = i + 1;
  if (!r.changed) return r;
  r.patched = true;
  r.first = follow_jump(e.code);
  HMODULE own = module_handle(e.code);
  const void* cur = r.first;
  const void* last = cur;
  for (int hop = 0; cur && hop < 8; ++hop) {   // Detours: E9 to a stub near the target, then jmp [rip] to the detour
    last = cur;
    HMODULE m = module_handle(cur);
    if (m && m != own && r.module.empty()) r.module = module_of(cur);
    cur = follow_jump(cur);
  }
  r.final = last;
  if (r.module.empty()) r.module = module_of(last);
  return r;
}

bool restore(Entry& e, const Patch& p) {
  if (!e.armed || !p.changed) return false;
  DWORD old = 0;
  if (!VirtualProtect(e.code, p.changed, PAGE_EXECUTE_READWRITE, &old)) return false;
  std::memcpy(e.code, e.orig, p.changed);
  DWORD dummy = 0;
  VirtualProtect(e.code, p.changed, old, &dummy);
  FlushInstructionCache(GetCurrentProcess(), e.code, p.changed);
  return std::memcmp(e.code, e.orig, kSnap) == 0;
}

}  // namespace bg3::guard
