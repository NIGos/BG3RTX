// Unit test of layer/hookguard.cpp: patches functions the way Microsoft
// Detours does on x64 (jmp rel32 to a stub allocated near the target, which
// does jmp [rip+0] to the detour) and checks detection, owner attribution and
// restoration. Exit code 0 when every check passes.
#include <cstdio>
#include <cstring>

#include "../layer/hookguard.h"

using namespace bg3::guard;

static int g_fail = 0;
#define CHECK(c)                                                         \
  do {                                                                   \
    if (!(c)) {                                                          \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);           \
      ++g_fail;                                                          \
    }                                                                    \
  } while (0)

extern "C" __declspec(noinline) int target_a(int x) {
  volatile int v = x;
  for (int i = 0; i < 3; ++i) v = v * 3 + i;
  return v + 1;
}
extern "C" __declspec(noinline) int target_b(int x) {
  volatile int v = x;
  for (int i = 0; i < 5; ++i) v = v ^ (i * 7);
  return v - 2;
}
extern "C" __declspec(noinline) int detour(int x) { return 1000 + x; }

// Executable memory within +-2 GB of `at`, like Detours' trampoline regions.
static uint8_t* alloc_near(const void* at) {
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  auto base = reinterpret_cast<uintptr_t>(at) & ~uintptr_t(0xFFFF);
  for (uintptr_t d = 0x10000; d < 0x70000000; d += 0x10000) {
    for (uintptr_t a : {base - d, base + d}) {
      void* p = VirtualAlloc(reinterpret_cast<void*>(a), si.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
      if (p) return static_cast<uint8_t*>(p);
    }
  }
  return nullptr;
}

// Detours-style inline patch of `code`: E9 rel32 to `stub`, int3 padding.
static void patch_jmp(uint8_t* code, const uint8_t* stub) {
  DWORD old;
  VirtualProtect(code, 8, PAGE_EXECUTE_READWRITE, &old);
  int32_t rel = int32_t(stub - (code + 5));
  code[0] = 0xE9;
  std::memcpy(code + 1, &rel, 4);
  std::memset(code + 5, 0xCC, 3);
  VirtualProtect(code, 8, old, &old);
  FlushInstructionCache(GetCurrentProcess(), code, 8);
}

static void write_indirect_jmp(uint8_t* stub, const void* to) {   // jmp [rip+0]; dq to
  stub[0] = 0xFF;
  stub[1] = 0x25;
  std::memset(stub + 2, 0, 4);
  std::memcpy(stub + 6, &to, 8);
  FlushInstructionCache(GetCurrentProcess(), stub, 14);
}

int main() {
  using Fn = int (*)(int);
  Fn volatile call_a = &target_a;
  Fn volatile call_b = &target_b;
  const int want_a = target_a(5), want_b = target_b(9);

  // 1. Untouched entries are not reported.
  Entry ea, eb;
  CHECK(snapshot(ea, "target_a", reinterpret_cast<const void*>(&target_a)));
  CHECK(snapshot(eb, "target_b", reinterpret_cast<const void*>(&target_b)));
  CHECK(!inspect(ea).patched);
  CHECK(!inspect(eb).patched);
  CHECK(follow_jump(nullptr) == nullptr);
  CHECK(module_matches(L"BG3ScriptExtender.dll", L"scriptextender"));
  CHECK(!module_matches(L"VkLayer_bg3rtx.dll", L"ScriptExtender"));

  // 2. Detours shape, detour in this module: E9 -> stub -> jmp [rip] -> detour.
  uint8_t* stub = alloc_near(ea.code);
  CHECK(stub != nullptr);
  if (!stub) return 1;
  write_indirect_jmp(stub, reinterpret_cast<const void*>(&detour));
  patch_jmp(ea.code, stub);
  CHECK(call_a(5) == 1005);   // the patch is live
  Patch pa = inspect(ea);
  CHECK(pa.patched);
  CHECK(pa.changed >= 5 && pa.changed <= 8);
  CHECK(pa.first == stub);
  // &detour may be an incremental-link thunk: the chain ends at the body.
  const void* detour_body = follow_jump(reinterpret_cast<const void*>(&detour));
  if (!detour_body) detour_body = reinterpret_cast<const void*>(&detour);
  CHECK(pa.final == detour_body);
  CHECK(module_matches(pa.module, L"hookguard_test"));
  CHECK(!module_matches(pa.module, L"ScriptExtender"));
  CHECK(restore(ea, pa));
  CHECK(!inspect(ea).patched);
  CHECK(call_a(5) == want_a);   // original code runs again

  // 3. Detour in another module: the owner is the first foreign module on the chain.
  const void* foreign = reinterpret_cast<const void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetTickCount"));
  CHECK(foreign != nullptr);
  uint8_t* stub2 = stub + 64;
  write_indirect_jmp(stub2, foreign);
  patch_jmp(eb.code, stub2);
  Patch pb = inspect(eb);
  CHECK(pb.patched);
  CHECK(pb.first == stub2);
  CHECK(module_matches(pb.module, L"kernel32"));
  CHECK(restore(eb, pb));
  CHECK(call_b(9) == want_b);

  // 4. mov rax, imm64; jmp rax written straight over the entry.
  {
    DWORD old;
    VirtualProtect(ea.code, 12, PAGE_EXECUTE_READWRITE, &old);
    ea.code[0] = 0x48;
    ea.code[1] = 0xB8;
    const void* d = reinterpret_cast<const void*>(&detour);
    std::memcpy(ea.code + 2, &d, 8);
    ea.code[10] = 0xFF;
    ea.code[11] = 0xE0;
    VirtualProtect(ea.code, 12, old, &old);
    FlushInstructionCache(GetCurrentProcess(), ea.code, 12);
  }
  CHECK(call_a(1) == 1001);
  Patch pc = inspect(ea);
  CHECK(pc.patched && pc.changed <= 12);
  CHECK(pc.first == reinterpret_cast<const void*>(&detour));
  CHECK(restore(ea, pc));
  CHECK(call_a(5) == want_a);

  // 5. A thunk inside our own module is followed at snapshot time, not taken for a patch.
  {
    uint8_t* thunk = stub + 128;   // not in a module: snapshot must stop at it
    write_indirect_jmp(thunk, reinterpret_cast<const void*>(&target_b));
    Entry et;
    CHECK(snapshot(et, "thunk", thunk));
    CHECK(et.code == thunk);
    CHECK(!inspect(et).patched);
  }

  VirtualFree(stub, 0, MEM_RELEASE);
  std::printf(g_fail ? "hookguard_test: %d FAILED\n" : "hookguard_test: all checks passed\n", g_fail);
  return g_fail ? 1 : 0;
}
