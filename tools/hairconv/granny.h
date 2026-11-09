#pragma once
// GR2 meshes through the player's own granny2.dll (docs/RTX_HAIR_ROADMAP.md 2f). granny2.dll (RAD Game Tools / Epic)
// cannot be redistributed and BG3 ships none, so it is never bundled: the player names a copy they already have
// (for example from their own LSLib ExportTool download) and bg3rtx_hairconv loads it at runtime. Without it, the
// "all styles" mode is unavailable and the capture model stays.
//
// As in LSLib (Norbyte, MIT), granny2.dll is used only to decompress the file's sections (BitKnit:
// GrannyBeginFileDecompression / GrannyDecompressIncremental / GrannyEndFileDecompression; Oodle: GrannyDecompressData);
// GrannyGetVersionString and GrannySetLogCallback are used for messages. Stock Granny 2.11 does not open BG3's GR2
// files itself (their 64-bit magic is not one it knows), so the header, section table and relocations are read here
// (LSLib GR2Reader) and the data tree is walked by its own type definitions, member by member: the mesh name,
// Position and TextureCoordinates0 of the primary vertex data in native order, the primary topology's indices, bone
// bindings and the skeletons' InverseWorld4x4.
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace hairconv::granny {

struct Mesh {
  std::string name;
  std::vector<float> pos, uv;               // [V * 3], [V * 2]
  std::vector<uint8_t> joints, weights;     // [V * 4] (0 when the vertex type has none)
  std::vector<uint32_t> indices;            // primary topology, native order
  std::vector<std::string> bindings;        // bone binding names
};
struct Bone { std::string name; float inverse_world[16]; };   // row-major as stored (LSLib InverseWorldTransform)
struct File {
  std::vector<Mesh> meshes;
  std::vector<Bone> bones;                  // every skeleton's bones, in order
};

class Library {
 public:
  ~Library();
  // LoadLibrary of the given path (never a search path); false with a reason when the file is not a usable x64
  // granny2.dll.
  bool load(const std::wstring& dll, std::string* error);
  bool loaded() const { return module_ != nullptr; }
  const std::string& version() const { return version_; }
  // Thread-safe (calls into granny are serialised; a GR2 parse is a few ms).
  bool read(const uint8_t* data, size_t size, File& out, std::string* error);

 private:
  using Begin = void* (*)(int format, bool reversed, int usize, void* dst, int work_size, void* work);
  using Incremental = bool (*)(void* state, int n, const void* src);
  using End = bool (*)(void* state);
  using Data = bool (*)(int format, bool reversed, int n, void* src, int stop0, int stop1, int stop2, void* dst);
  bool decompress(uint32_t format, const uint8_t* src, uint32_t n, uint8_t* dst, uint32_t usize, uint32_t stop0, uint32_t stop1);
  void* module_ = nullptr;
  Begin begin_ = nullptr;
  Incremental incremental_ = nullptr;
  End end_ = nullptr;
  Data data_ = nullptr;
  std::string version_;
  std::mutex mu_;
};

// Writes File in the tools/hair_groom/gr2_dump.ps1 layout ('GR2D' v1): the dev equivalence check compares it byte for
// byte with the Python batch's cached LSLib dumps.
std::vector<uint8_t> dump_bytes(const File& f);

}  // namespace hairconv::granny
