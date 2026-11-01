// Persistent SPIR-V cache of the runtime shader path (layer/shaders.cpp): the C++ twin of the key
// logic of tools/shader_identity.py (git_blob_id, SourceTree.closure, entry_key) and a small
// content-addressed store on disk. tests/shader_cache_test.cpp checks the keys against the golden
// file that tests/shader_cache_key_test.py computes with the Python module.
//
// Key of one entry = sha256(json [schema, tool id, request, entry, profile, deps]); deps = the
// sorted (logical path, git blob id or null) of the source and of every file it can include,
// transitively, plus every missing include candidate. The runtime tool id (dxcompiler.dll bytes,
// its version, the argument template, SHaRC presence, shader_debug) differs from the offline one
// (dxc.exe): the two never share entries, only the key function.
//
// Store: <dir>\<key>.spv = 16-byte header (magic, schema, payload bytes, FNV-1a 32 of the payload)
// + SPIR-V. A file whose size, header, checksum or SPIR-V magic is wrong is a miss and is deleted.
// Writes go to a temporary file renamed over the target (a crash never leaves a torn entry).
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace bg3::shader_cache {

constexpr int kSchema = 1;   // tools/shader_identity.py SCHEMA

std::string sha1_hex(const void* data, size_t n);
std::string sha256_hex(const void* data, size_t n);
std::string git_blob_id(const std::string& data);     // sha1("blob <n>\0" + data)
std::string json_string(const std::string& utf8);     // json.dumps(str) with ensure_ascii
std::string posix_normpath(const std::string& p);     // posixpath.normpath
std::string posix_dirname(const std::string& p);      // posixpath.dirname

using Deps = std::vector<std::pair<std::string, std::optional<std::string>>>;

// Files by logical posix path ("shaders/gi/x.hlsl", "common/y.h", "sharc/SharcCommon.h").
class Tree {
 public:
  void add(const std::string& logical, std::string data);
  bool has(const std::string& p) const { return ids_.count(p) != 0; }
  size_t size() const { return ids_.size(); }
  // SourceTree.closure: conservative (every #include line, every existing search candidate
  // followed, every missing one recorded; a macro include depends on every file).
  Deps closure(const std::string& path) const;
  // shaders/ and common/ under `root` and, when `sharc_dir` is not empty, its files as sharc/
  // (SourceTree.worktree + with_sharc).
  static std::shared_ptr<Tree> from_disk(const std::string& root, const std::string& sharc_dir);

 private:
  std::vector<std::string> includes() const;
  std::map<std::string, std::string> ids_, data_;
  mutable std::mutex mu_;
  mutable std::map<std::string, Deps> closure_;
};

std::string entry_key(const std::string& tool_id, const std::string& request, const std::string& entry,
                      const Deps& deps, const std::string& profile);
// The key of `request` ("<file>?NAME=VALUE;...") in `tree`; empty when its file is not there.
std::string request_key(const Tree& tree, const std::string& tool_id, const std::string& request,
                        const std::string& entry, const std::string& profile);

// ---- the store ----
bool ensure_dir(const std::string& dir);
// False on a miss; a corrupt entry is deleted. A hit refreshes the file's write time (LRU).
bool load(const std::string& dir, const std::string& key, std::vector<uint32_t>& out);
bool store(const std::string& dir, const std::string& key, const std::vector<uint32_t>& code);
struct PruneStats {
  size_t files = 0;
  uint64_t bytes = 0;
  size_t removed = 0;
};
// Above `cap_bytes`, deletes the least recently used entries down to 3/4 of it; deletes
// temporary files older than an hour.
PruneStats prune(const std::string& dir, uint64_t cap_bytes);

}  // namespace bg3::shader_cache
