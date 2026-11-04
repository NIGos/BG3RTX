#include <cstdio>
#include <cstdlib>
#include <io.h>
#include <cstdint>
#include "../layer/checked_file.h"

struct FakeCom {
  unsigned released = 0;
  void Release() { ++released; }
};
using IDxcUtils = FakeCom;
using IDxcCompiler3 = FakeCom;
using IDxcIncludeHandler = FakeCom;
#include "../layer/dxc_thread.h"
#define CHECK(x) do { if (!(x)) { printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
  FakeCom utils, compiler, include;
  {
    bg3::Dxc d;
    d.utils = &utils; d.compiler = &compiler; d.include = &include;
  }
  CHECK(utils.released == 1 && compiler.released == 1 && include.released == 1);
  {
    bg3::Dxc partial;
    partial.utils = &utils;
    partial.reset(); partial.reset();
  }
  CHECK(utils.released == 2); // failed init and repeated explicit/thread-exit cleanup
  FILE* f = tmpfile(); CHECK(f);
  CHECK(bg3::write_and_close(f, "abc", 3));
  f = tmpfile(); CHECK(f);
  CHECK(bg3::write_and_close(f, "", 0));
  // The buffer accepts bytes, but closing cannot flush its invalid descriptor.
  f = tmpfile(); CHECK(f);
  char buffer[4096]; CHECK(setvbuf(f, buffer, _IOFBF, sizeof buffer) == 0);
  CHECK(_close(_fileno(f)) == 0);
  // Expected invalid-descriptor failure must return an error, not terminate the test.
  const auto handler = _set_invalid_parameter_handler(
      [](const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {});
  const bool failed_close = !bg3::write_and_close(f, "abc", 3);
  _set_invalid_parameter_handler(handler);
  CHECK(failed_close);
  printf("layer CPU: COM ownership and buffered write failure passed\n");
  return 0;
}
