// Persistent SPIR-V cache of the runtime shader path: keys (the twin of tools/shader_identity.py)
// and the store. No dependency on the rest of the layer (tests/shader_cache_test.cpp links it alone).
#include "shader_cache.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstring>

#pragma comment(lib, "bcrypt.lib")

namespace bg3::shader_cache {

namespace {

struct Part {
  const void* p;
  size_t n;
};

std::string hash_hex(LPCWSTR alg, std::initializer_list<Part> parts) {
  BCRYPT_ALG_HANDLE a = nullptr;
  BCRYPT_HASH_HANDLE h = nullptr;
  unsigned char out[64] = {};
  DWORD len = 0, got = 0;
  std::string hex;
  if (BCryptOpenAlgorithmProvider(&a, alg, nullptr, 0) != 0) return hex;
  if (BCryptGetProperty(a, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&len), sizeof len, &got, 0) == 0 &&
      len <= sizeof out && BCryptCreateHash(a, &h, nullptr, 0, nullptr, 0, 0) == 0) {
    bool ok = true;
    for (const Part& part : parts) {
      const unsigned char* p = static_cast<const unsigned char*>(part.p);
      for (size_t left = part.n; ok && left;) {
        const ULONG step = ULONG((std::min)(left, size_t(1) << 30));
        ok = BCryptHashData(h, const_cast<PUCHAR>(p), step, 0) == 0;
        p += step;
        left -= step;
      }
    }
    if (ok && BCryptFinishHash(h, out, len, 0) == 0) {
      static const char kHex[] = "0123456789abcdef";
      for (DWORD i = 0; i < len; ++i) hex += kHex[out[i] >> 4], hex += kHex[out[i] & 15];
    }
    BCryptDestroyHash(h);
  }
  BCryptCloseAlgorithmProvider(a, 0);
  return hex;
}

// Python's str.strip() for the ASCII whitespace it removes.
bool py_space(unsigned char c) { return c == ' ' || (c >= 9 && c <= 13) || (c >= 0x1c && c <= 0x1f); }

std::string py_strip(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && py_space(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && py_space(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

// posixpath.join(d, spec) for one component.
std::string posix_join(const std::string& d, const std::string& spec) {
  if (!spec.empty() && spec[0] == '/') return spec;
  if (d.empty() || d.back() == '/') return d + spec;
  return d + "/" + spec;
}

size_t skip_blank(const std::string& s, size_t i) {
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
  return i;
}

// At a line start: "[ \t]*#[ \t]*include", the position after it or npos.
size_t after_include(const std::string& s, size_t i) {
  i = skip_blank(s, i);
  if (i >= s.size() || s[i] != '#') return std::string::npos;
  i = skip_blank(s, i + 1);
  if (s.compare(i, 7, "include") != 0) return std::string::npos;
  return i + 7;
}

// INCLUDE_RE (findall) and MACRO_INCLUDE_RE (search) of tools/shader_identity.py.
void scan(const std::string& s, std::vector<std::pair<char, std::string>>& incs, bool& macro) {
  macro = false;
  for (size_t line = 0; line <= s.size();) {
    const size_t at = after_include(s, line);
    if (at != std::string::npos) {
      // '[ \t]*([<"])([^>"\r\n]+)[>"]'
      size_t i = skip_blank(s, at);
      if (i < s.size() && (s[i] == '<' || s[i] == '"')) {
        const char kind = s[i];
        size_t j = i + 1;
        while (j < s.size() && s[j] != '>' && s[j] != '"' && s[j] != '\r' && s[j] != '\n') ++j;
        if (j > i + 1 && j < s.size() && (s[j] == '>' || s[j] == '"')) incs.emplace_back(kind, s.substr(i + 1, j - i - 1));
      }
      // '[ \t]+[^<"\s]'
      i = skip_blank(s, at);
      if (i > at && i < s.size()) {
        const char c = s[i];
        if (c != '<' && c != '"' && c != '\n' && c != '\r' && c != '\f' && c != '\v') macro = true;
      }
    }
    const size_t nl = s.find('\n', line);
    if (nl == std::string::npos) break;
    line = nl + 1;
  }
}

std::wstring widen(const std::string& s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(n ? n - 1 : 0, L'\0');
  if (n) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
  return w;
}

std::string narrow(const std::wstring& w) {
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
  std::string s(n ? n - 1 : 0, '\0');
  if (n) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
  return s;
}

bool read_all(const std::wstring& path, std::string& out) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  bool ok = GetFileSizeEx(h, &size) && size.QuadPart < (int64_t(1) << 31);
  if (ok) {
    out.resize(size_t(size.QuadPart));
    DWORD got = 0;
    ok = out.empty() || (ReadFile(h, out.data(), DWORD(out.size()), &got, nullptr) && got == out.size());
  }
  CloseHandle(h);
  return ok;
}

void walk(const std::wstring& dir, const std::string& logical, Tree& t) {
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if (!std::wcscmp(fd.cFileName, L".") || !std::wcscmp(fd.cFileName, L"..")) continue;
    const std::wstring path = dir + L"\\" + fd.cFileName;
    const std::string name = logical + "/" + narrow(fd.cFileName);
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      walk(path, name, t);
      continue;
    }
    std::string data;
    if (read_all(path, data)) t.add(name, std::move(data));
  } while (FindNextFileW(h, &fd));
  FindClose(h);
}

uint32_t fnv1a32(const void* p, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<const unsigned char*>(p)[i]) * 16777619u;
  return h;
}

constexpr uint32_t kMagic = 0x43533342u;   // "B3SC"
constexpr uint32_t kSpirvMagic = 0x07230203u;

std::string entry_path(const std::string& dir, const std::string& key) { return dir + "\\" + key + ".spv"; }

}  // namespace

std::string sha1_hex(const void* data, size_t n) { return hash_hex(BCRYPT_SHA1_ALGORITHM, {{data, n}}); }
std::string sha256_hex(const void* data, size_t n) { return hash_hex(BCRYPT_SHA256_ALGORITHM, {{data, n}}); }

std::string git_blob_id(const std::string& data) {
  const std::string head = "blob " + std::to_string(data.size());
  return hash_hex(BCRYPT_SHA1_ALGORITHM, {{head.data(), head.size() + 1}, {data.data(), data.size()}});
}

std::string json_string(const std::string& s) {
  std::string out = "\"";
  char buf[8];
  auto u = [&](unsigned v) {
    snprintf(buf, sizeof buf, "\\u%04x", v);
    out += buf;
  };
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      ++i;
      if (c == '"') out += "\\\"";
      else if (c == '\\') out += "\\\\";
      else if (c == '\n') out += "\\n";
      else if (c == '\r') out += "\\r";
      else if (c == '\t') out += "\\t";
      else if (c == '\b') out += "\\b";
      else if (c == '\f') out += "\\f";
      else if (c < 0x20 || c == 0x7f) u(c);
      else out += char(c);
      continue;
    }
    // UTF-8 -> code point -> \uXXXX (surrogate pairs above the BMP), as ensure_ascii.
    const int len = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
    unsigned cp = len == 4 ? c & 7u : len == 3 ? c & 15u : len == 2 ? c & 31u : 0xfffdu;
    for (int k = 1; k < len && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 63u);
    i += size_t(len);
    if (cp >= 0x10000) {
      cp -= 0x10000;
      u(0xd800 + (cp >> 10));
      u(0xdc00 + (cp & 0x3ff));
    } else {
      u(cp);
    }
  }
  return out + "\"";
}

std::string posix_normpath(const std::string& path) {
  if (path.empty()) return ".";
  int initial = path[0] == '/' ? 1 : 0;
  if (initial && path.compare(0, 2, "//") == 0 && path.compare(0, 3, "///") != 0) initial = 2;
  std::vector<std::string> comps;
  for (size_t a = 0; a <= path.size();) {
    size_t b = path.find('/', a);
    if (b == std::string::npos) b = path.size();
    const std::string comp = path.substr(a, b - a);
    a = b + 1;
    if (comp.empty() || comp == ".") continue;
    if (comp != ".." || (!initial && comps.empty()) || (!comps.empty() && comps.back() == ".."))
      comps.push_back(comp);
    else if (!comps.empty())
      comps.pop_back();
  }
  std::string out(size_t(initial), '/');
  for (size_t i = 0; i < comps.size(); ++i) out += (i ? "/" : "") + comps[i];
  return out.empty() ? "." : out;
}

std::string posix_dirname(const std::string& p) {
  const size_t i = p.rfind('/') + 1;   // npos + 1 == 0
  std::string head = p.substr(0, i);
  if (!head.empty() && head.find_first_not_of('/') != std::string::npos)
    head.erase(head.find_last_not_of('/') + 1);
  return head;
}

void Tree::add(const std::string& logical, std::string data) {
  ids_[logical] = git_blob_id(data);
  data_[logical] = std::move(data);
  std::lock_guard<std::mutex> lock(mu_);
  closure_.clear();
}

std::vector<std::string> Tree::includes() const {
  std::vector<std::string> dirs{"shaders"};
  auto it = ids_.lower_bound("sharc/");
  if (it != ids_.end() && it->first.compare(0, 6, "sharc/") == 0) dirs.push_back("sharc");
  return dirs;
}

Deps Tree::closure(const std::string& path) const {
  {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = closure_.find(path);
    if (it != closure_.end()) return it->second;
  }
  const std::vector<std::string> dirs = includes();
  std::map<std::string, std::optional<std::string>> seen;
  std::vector<std::string> stack{path};
  std::vector<std::pair<char, std::string>> incs;
  while (!stack.empty()) {
    const std::string p = stack.back();
    stack.pop_back();
    if (seen.count(p)) continue;
    auto id = ids_.find(p);
    if (id == ids_.end()) {
      seen[p] = std::nullopt;
      continue;
    }
    seen[p] = id->second;
    const std::string& data = data_.at(p);
    bool macro = false;
    incs.clear();
    scan(data, incs, macro);
    if (macro) {
      std::string all;
      for (const auto& [k, v] : ids_) all += k + "=" + v + "\n";
      seen["*all*"] = sha256_hex(all.data(), all.size());
    }
    for (auto& [kind, raw] : incs) {
      std::string spec = py_strip(raw);
      std::replace(spec.begin(), spec.end(), '\\', '/');
      std::vector<std::string> cands;
      if (kind == '"') cands.push_back(posix_dirname(p));
      cands.insert(cands.end(), dirs.begin(), dirs.end());
      for (const std::string& d : cands) stack.push_back(posix_normpath(posix_join(d, spec)));
    }
  }
  Deps deps(seen.begin(), seen.end());
  std::lock_guard<std::mutex> lock(mu_);
  closure_[path] = deps;
  return deps;
}

std::shared_ptr<Tree> Tree::from_disk(const std::string& root, const std::string& sharc_dir) {
  auto t = std::make_shared<Tree>();
  const std::wstring r = widen(root);
  walk(r + L"\\shaders", "shaders", *t);
  walk(r + L"\\common", "common", *t);
  if (!sharc_dir.empty()) walk(widen(sharc_dir), "sharc", *t);
  return t;
}

std::string entry_key(const std::string& tool_id, const std::string& request, const std::string& entry,
                      const Deps& deps, const std::string& profile) {
  std::string j = "[" + std::to_string(kSchema) + ", " + json_string(tool_id) + ", " + json_string(request) + ", " +
                  json_string(entry) + ", " + json_string(profile) + ", [";
  for (size_t i = 0; i < deps.size(); ++i)
    j += (i ? ", [" : "[") + json_string(deps[i].first) + ", " + (deps[i].second ? json_string(*deps[i].second) : "null") + "]";
  j += "]]";
  return sha256_hex(j.data(), j.size());
}

std::string request_key(const Tree& tree, const std::string& tool_id, const std::string& request,
                        const std::string& entry, const std::string& profile) {
  const std::string path = "shaders/" + request.substr(0, request.find('?'));
  if (!tree.has(path)) return {};
  return entry_key(tool_id, request, entry, tree.closure(path), profile);
}

bool ensure_dir(const std::string& dir) {
  if (dir.empty()) return false;
  for (size_t i = dir.find_first_of("\\/", 3); i != std::string::npos; i = dir.find_first_of("\\/", i + 1))
    CreateDirectoryW(widen(dir.substr(0, i)).c_str(), nullptr);
  CreateDirectoryW(widen(dir).c_str(), nullptr);
  const DWORD a = GetFileAttributesW(widen(dir).c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool load(const std::string& dir, const std::string& key, std::vector<uint32_t>& out) {
  const std::wstring path = widen(entry_path(dir, key));
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  bool ok = GetFileSizeEx(h, &size) && size.QuadPart >= 16 + 20 && size.QuadPart < (int64_t(1) << 30) &&
            (size.QuadPart - 16) % 4 == 0;
  uint32_t head[4] = {};
  std::vector<uint32_t> code;
  DWORD got = 0;
  if (ok) ok = ReadFile(h, head, sizeof head, &got, nullptr) && got == sizeof head;
  if (ok) ok = head[0] == kMagic && head[1] == uint32_t(kSchema) && head[2] == uint64_t(size.QuadPart) - 16;
  if (ok) {
    code.resize(head[2] / 4);
    ok = ReadFile(h, code.data(), head[2], &got, nullptr) && got == head[2] && code[0] == kSpirvMagic &&
         fnv1a32(code.data(), head[2]) == head[3];
  }
  if (ok) {
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    SetFileTime(h, nullptr, nullptr, &now);
  }
  CloseHandle(h);
  if (!ok) {
    DeleteFileW(path.c_str());
    return false;
  }
  out.swap(code);
  return true;
}

bool store(const std::string& dir, const std::string& key, const std::vector<uint32_t>& code) {
  if (code.size() < 5 || code[0] != kSpirvMagic) return false;
  const std::string final_path = entry_path(dir, key);
  const std::string tmp = final_path + "." + std::to_string(GetCurrentProcessId()) + "." +
                          std::to_string(GetCurrentThreadId()) + ".tmp";
  const std::wstring wtmp = widen(tmp);
  HANDLE h = CreateFileW(wtmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  const uint32_t bytes = uint32_t(code.size() * 4);
  const uint32_t head[4] = {kMagic, uint32_t(kSchema), bytes, fnv1a32(code.data(), bytes)};
  DWORD put = 0, put2 = 0;
  bool ok = WriteFile(h, head, sizeof head, &put, nullptr) && put == sizeof head &&
            WriteFile(h, code.data(), bytes, &put2, nullptr) && put2 == bytes;
  CloseHandle(h);
  ok = ok && MoveFileExW(wtmp.c_str(), widen(final_path).c_str(), MOVEFILE_REPLACE_EXISTING);
  if (!ok) DeleteFileW(wtmp.c_str());
  return ok;
}

PruneStats prune(const std::string& dir, uint64_t cap_bytes) {
  struct Item {
    uint64_t time, size;
    std::wstring path;
  };
  std::vector<Item> items;
  PruneStats st;
  const std::wstring wdir = widen(dir);
  FILETIME now_ft;
  GetSystemTimeAsFileTime(&now_ft);
  const uint64_t now = (uint64_t(now_ft.dwHighDateTime) << 32) | now_ft.dwLowDateTime;
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW((wdir + L"\\*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return st;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    const std::wstring name = fd.cFileName;
    const uint64_t t = (uint64_t(fd.ftLastWriteTime.dwHighDateTime) << 32) | fd.ftLastWriteTime.dwLowDateTime;
    const uint64_t size = (uint64_t(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
    auto ends = [&](const wchar_t* ext) {
      const size_t n = std::wcslen(ext);
      return name.size() > n && name.compare(name.size() - n, n, ext) == 0;
    };
    if (ends(L".tmp")) {
      if (now > t + 36000000000ull && DeleteFileW((wdir + L"\\" + name).c_str())) ++st.removed;   // 1 h
      continue;
    }
    if (!ends(L".spv")) continue;
    items.push_back({t, size, wdir + L"\\" + name});
    st.bytes += size;
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  st.files = items.size();
  if (st.bytes <= cap_bytes) return st;
  std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.time < b.time; });
  for (const Item& it : items) {
    if (st.bytes <= cap_bytes / 4 * 3) break;
    if (DeleteFileW(it.path.c_str())) {
      st.bytes -= it.size;
      --st.files;
      ++st.removed;
    }
  }
  return st;
}

}  // namespace bg3::shader_cache
