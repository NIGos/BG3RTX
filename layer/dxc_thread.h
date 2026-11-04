#pragma once

// Included after the DXC interfaces. Each thread owns its COM references.
namespace bg3 {
struct Dxc {
  IDxcUtils* utils = nullptr;
  IDxcCompiler3* compiler = nullptr;
  IDxcIncludeHandler* include = nullptr;
  bool tried = false;
  void reset() {
    if (include) include->Release();
    if (compiler) compiler->Release();
    if (utils) utils->Release();
    include = nullptr;
    compiler = nullptr;
    utils = nullptr;
    tried = false;
  }
  ~Dxc() { reset(); }
};
}  // namespace bg3
