#pragma once
// One style through the converter (batch.process / hcap.reference), shared by the capture mode (main.cpp) and the
// all-styles mode (all.cpp).
#include "hairconv.h"

#include <filesystem>
#include <string>
#include <vector>

namespace hairconv {

constexpr const char* kVersion = "hairconv-1";

struct Rec {
  std::string name, verdict, asset, json;
  std::vector<std::string> reasons;
  int strands = 0;
  uint64_t bytes = 0;
  double seconds = 0;
  std::string index_id;   // 16 hex digits, every verdict (the layer's join key)
  int indices = 0;
};

struct Options {
  std::string out_dir;
  int threads = 4;
  bool full = true, accept_review = false;
};

// grid_ref: the mesh the atlas grid is detected on (LOD1 rows: their LOD0 mesh, batch.py read_cards); null = in.mesh.
// in.has_grid still overrides it. extra_json is appended to the record (", \"key\": value" pairs).
Rec process(const std::string& name, const Input& in, const Mesh* grid_ref, const Options& o, const std::string& extra_json = {});
Rec reject(const std::string& name, const std::string& reason, const std::string& extra_json = {});

std::string json_escape(const std::string& s);
bool write_text(const std::filesystem::path& p, const std::string& text);   // tmp + rename
std::string read_text(const std::filesystem::path& p);

// bg3rtx_hairconv --all (all.cpp). argv after the program name; returns the exit code.
int run_all(int argc, char** argv);

}  // namespace hairconv
