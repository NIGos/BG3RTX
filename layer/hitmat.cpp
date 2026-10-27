// EN2: material data at secondary ray hits (docs/TRACK_D_RT.md 13, docs/AUDIT_ENGINE_DATA.md
// 2.1 route a1). rt_hitmat=1 (needs rt_alpha=1 and the BVH split).
//
// The ray tracing scene comes from the depth prepass, whose opaque draws carry no material
// and no UV. The material is in the G-buffer pass, which draws the same meshes later in the
// frame. Per G-buffer draw:
//   1. join: the draw's geometry (vertex buffer 0, index range, counts, instances;
//      draw_geom_key) and its occurrence in the frame name one prepass draw of this frame's
//      BVH build (bvh_join_frame), i.e. the TLAS instances it became;
//   2. material: its fragment shader's line of shaders/rt/material_fs.txt
//      (scripts/material_table.py) says which bound views, UBO constants and SSBO tile-set
//      entry make RT1.rgb / RT2.xyz; they are read here, from the game's mapped memory and
//      its descriptor bindings, into a 256-byte record (shaders/rt/rt_hitmat.hlsli);
//   3. UV: the opaque prepass capture holds no UV (its vertex shaders output none), so the
//      draw is captured by transform feedback here (the G-buffer VS's lowest vec2 output,
//      the one its FS samples with) and shaders/rt/uv_scatter.hlsl copies the UV into the
//      UV slots of the same triangles: the prepass soup for a dynamic draw (every frame),
//      the mesh pool for a cached mesh (once per mesh). Alpha-tested draws keep their
//      prepass UV (their prepass VS outputs it).
// Per scene slot (set 1 bindings 10-12): the records, one entry per TLAS instance
// (record, family, UV valid) and the hit statistics. A slot's region is cleared when the
// slot is committed (alpha_commit -> hitmat_on_commit), so a TLAS never sees another
// build's entries; the statistics of its last use are read back then (the GPU is done).
#include "host.h"
#include "assetdb.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace bg3 {

namespace {

constexpr uint32_t kRecBytes = 256;
constexpr uint32_t kMaxRecs = 8192;
constexpr uint32_t kMaxJobs = 8192;
constexpr uint32_t kFamCount = 1024;            // rt_hitmat.hlsli kFamCount
constexpr uint32_t kQaCount = 64;
constexpr uint32_t kNone = UINT32_MAX;
constexpr uint32_t kSrcNone = 0, kSrcVt = 1, kSrcTex = 2, kSrcConst = 3;   // rt_hitmat.hlsli
constexpr uint32_t kHmAlbedo = 1, kHmRough = 2, kHmMetal = 4, kHmRefl = 8;
constexpr uint32_t kInstUv = 1;                 // rt_hitmat.hlsli kHmInstUv

// One field of a material_fs.txt line.
struct Field {
  uint8_t kind = 0;          // 0 none, 1 vt, 2 tex, 3 const, 4 ubo scalar (read per draw -> const)
  uint8_t set = 0, binding = 0;
  uint8_t ich = 0, col = 0;
  uint8_t swz[3] = {0, 1, 2};
  uint8_t n = 1;             // components (albedo 3)
  float value = 0;
  uint32_t off = 0;          // ubo
};
struct MatFs {
  uint8_t kind = 0;          // 1 vt, 2 tex, 3 constants only
  int uv = -1;
  uint8_t ubo_set = 0, ubo_bind = 0, ssbo_set = 0, ssbo_bind = 0, ind_set = 0, ind_bind = 0;
  uint32_t p0 = 0, idx = 0;
  uint8_t uvs_kind = 0;      // tex: 0 none, 3 const, 4 ubo vec2
  uint8_t uvs_set = 0, uvs_bind = 0;
  uint32_t uvs_off = 0;
  float uvs_k = 1;
  Field f[4];                // albedo, roughness, metalness, reflectance
  Field albk;                // albedo factor (kind 0: none)
};

struct MatRec {
  uint32_t kind, known, ind, fs_lo;
  uint32_t f[4][4];
  float albk[4];
  float p0[4], p1[4];
  float t[32];
};
static_assert(sizeof(MatRec) == kRecBytes, "MatRec must match rt_hitmat.hlsli");

struct Buf {
  VkBuffer b = VK_NULL_HANDLE;
  VkDeviceMemory m = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
  uint8_t* map = nullptr;
};

DeviceData* g_dd = nullptr;
std::mutex g_mu;
bool g_ready = false, g_failed = false;
std::unordered_map<uint64_t, MatFs> g_table;
uint64_t g_table_mtime = 0;

// Per-slot regions (host-visible): records, instance entries, statistics, scatter jobs.
Buf g_slots;
VkDeviceSize g_rec_region = 0, g_inst_region = 0, g_stats_region = 0, g_job_region = 0, g_slot_bytes = 0;
uint32_t g_max_inst = 0;
VkDeviceSize off_rec(uint32_t s) { return VkDeviceSize(s) * g_slot_bytes; }
VkDeviceSize off_inst(uint32_t s) { return off_rec(s) + g_rec_region; }
VkDeviceSize off_stats(uint32_t s) { return off_inst(s) + g_inst_region; }
VkDeviceSize off_jobs(uint32_t s) { return off_stats(s) + g_stats_region; }
Buf g_xfb;   // this frame's G-buffer UV capture (device-local)

PFN_vkCmdBindTransformFeedbackBuffersEXT p_bind = nullptr;
PFN_vkCmdBeginTransformFeedbackEXT p_begin = nullptr;
PFN_vkCmdEndTransformFeedbackEXT p_end = nullptr;
VkDescriptorSetLayout g_dsl = VK_NULL_HANDLE;
VkPipelineLayout g_pl = VK_NULL_HANDLE;
VkPipeline g_pipe = VK_NULL_HANDLE;
uint64_t g_pipe_gen = 0;

// Families: one per G-buffer fragment shader seen (0 unjoined, 1 legacy: rt_hitmat.hlsli).
std::unordered_map<uint64_t, uint32_t> g_family;
std::vector<uint64_t> g_family_fs = {0, 0};

// Cached meshes whose pool UV is the G-buffer's: mesh key -> generation, build record.
struct Filled { uint64_t gen, rec, frame; };
std::unordered_map<uint64_t, Filled> g_filled;

// This frame's G-buffer join.
struct ViewReq { VkImageView view; VkImageLayout layout; bool array; };
struct PendingRec { MatRec r; int view_ind; int view_f[4]; };   // indices into g_views (-1 none)
struct Job { uint32_t src, dst, verts, target; };
struct Frame {
  uint64_t frame = UINT64_MAX;
  bool ok = false;
  BvhJoinFrame join;
  std::unordered_map<uint64_t, std::vector<uint32_t>> by_key;
  std::unordered_map<uint64_t, uint32_t> occ;
  std::vector<PendingRec> recs;
  std::vector<ViewReq> views;
  std::vector<std::pair<uint32_t, uint32_t>> entries;   // instance, entry .y (record index set at flush)
  std::vector<uint32_t> entry_rec;                      // pending record index per entry (kNone)
  std::vector<Job> jobs;
  std::vector<std::pair<uint64_t, uint64_t>> filled_now;   // mesh key, gen
  VkDeviceSize cursor = 0;
  uint32_t recs_written = 0;
  bool xfb_active = false;
  bool pipe_ok = false;
  bool cmd_rec_set = false;
  uint64_t cmd_rec = 0;
};
Frame g_fr;

// Statistics (log every rt_timing_frames).
struct Stats {
  uint64_t frames = 0, draws = 0, joined = 0, unjoined = 0, not_in_tlas = 0, indirect = 0, no_table = 0,
           recs = 0, rec_fail[6] = {}, uv_prepass = 0, uv_cached = 0, uv_captured = 0, uv_fail[6] = {}, frames_joined = 0;
  uint64_t fam[kFamCount][4] = {};
  uint64_t qa[kQaCount] = {};
} g_st;
const char* kRecFail[6] = {"no view", "UBO", "SSBO", "non-finite", "records full", "views full"};
const char* kUvFail[6] = {"no XFB pipeline", "not a triangle list", "UV location differs", "triangle count differs",
                          "capture buffer full", "jobs full"};

bool make_buf(Buf& out, VkDeviceSize size, VkBufferUsageFlags usage, bool host, const char* what) {
  DeviceData* dd = g_dd;
  out = Buf{};
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = size;
  bci.usage = usage;
  if (dd->vk.CreateBuffer(dd->device, &bci, nullptr, &out.b) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  dd->vk.GetBufferMemoryRequirements(dd->device, out.b, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  const VkMemoryPropertyFlags hv = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  mai.memoryTypeIndex = host ? find_memory_type(dd, req.memoryTypeBits, hv | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
                             : find_memory_type(dd, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (host && mai.memoryTypeIndex == UINT32_MAX) mai.memoryTypeIndex = find_memory_type(dd, req.memoryTypeBits, hv);
  if (mai.memoryTypeIndex == UINT32_MAX || dd->vk.AllocateMemory(dd->device, &mai, nullptr, &out.m) != VK_SUCCESS) {
    dd->vk.DestroyBuffer(dd->device, out.b, nullptr);
    out = Buf{};
    return false;
  }
  dd->vk.BindBufferMemory(dd->device, out.b, out.m, 0);
  out.size = size;
  if (host) {
    void* p = nullptr;
    if (dd->vk.MapMemory(dd->device, out.m, 0, VK_WHOLE_SIZE, 0, &p) != VK_SUCCESS) {
      dd->vk.DestroyBuffer(dd->device, out.b, nullptr);
      dd->vk.FreeMemory(dd->device, out.m, nullptr);
      out = Buf{};
      return false;
    }
    out.map = static_cast<uint8_t*>(p);
  }
  log("[hitmat] %s: %.1f MB%s", what, double(size) / (1 << 20), host ? " (host-visible)" : "");
  return true;
}

void free_buf(Buf& b) {
  if (b.b) defer_destroy(BG3_DEFER_BUFFER, uint64_t(b.b));
  if (b.m) defer_destroy(BG3_DEFER_MEMORY, uint64_t(b.m));
  b = Buf{};
}

// "vt:1.5:0:0:xyz", "tex:1.5:xyz", "c:0.5", "u:1.1:16", "-"
bool parse_field(const std::string& v, Field& f, uint8_t n) {
  f = Field{};
  f.n = n;
  if (v == "-" || v.empty()) return true;
  std::vector<std::string> p;
  std::stringstream ss(v);
  for (std::string t; std::getline(ss, t, ':');) p.push_back(t);
  auto sb = [](const std::string& s, uint8_t& a, uint8_t& b) {
    size_t d = s.find('.');
    if (d == std::string::npos) return false;
    a = uint8_t(std::stoul(s.substr(0, d)));
    b = uint8_t(std::stoul(s.substr(d + 1)));
    return true;
  };
  auto swz = [&](const std::string& s) {
    if (s.size() < n) return false;
    for (uint8_t k = 0; k < n; ++k) {
      size_t i = std::string("xyzw").find(s[k]);
      if (i == std::string::npos) return false;
      f.swz[k] = uint8_t(i);
    }
    return true;
  };
  try {
    if (p[0] == "c" && p.size() >= 2) {
      f.kind = 3;
      f.value = std::stof(p[1]);
      return true;
    }
    if (p[0] == "u" && p.size() >= 3) {
      f.kind = 4;
      if (!sb(p[1], f.set, f.binding)) return false;
      f.off = uint32_t(std::stoul(p[2]));
      if (p.size() >= 4) f.n = uint8_t(std::stoul(p[3]));
      return true;
    }
    if (p[0] == "vt" && p.size() >= 5) {
      f.kind = 1;
      if (!sb(p[1], f.set, f.binding)) return false;
      f.ich = uint8_t(std::stoul(p[2]));
      f.col = uint8_t(std::stoul(p[3]));
      return f.ich < 4 && f.col < 4 && swz(p[4]);
    }
    if (p[0] == "tex" && p.size() >= 3) {
      f.kind = 2;
      return sb(p[1], f.set, f.binding) && swz(p[2]);
    }
  } catch (...) {
  }
  return false;
}

void load_table() {
  const std::string path = root_dir() + "\\shaders\\rt\\material_fs.txt";
  const uint64_t mt = file_mtime(path);
  if (!mt || mt == g_table_mtime) return;
  g_table_mtime = mt;
  std::ifstream in(path);
  std::unordered_map<uint64_t, MatFs> t;
  std::string line;
  uint32_t bad = 0;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ls(line);
    std::string hash, kind, kv;
    ls >> hash >> kind;
    MatFs m;
    m.kind = kind == "vt" ? 1 : kind == "tex" ? 2 : kind == "const" ? 3 : 0;
    bool ok = m.kind != 0;
    auto sb = [](const std::string& v, uint8_t& s, uint8_t& b) {
      size_t d = v.find('.');
      s = uint8_t(std::stoul(v.substr(0, d)));
      b = uint8_t(std::stoul(v.substr(d + 1)));
    };
    try {
      while (ok && (ls >> kv)) {
        size_t e = kv.find('=');
        if (e == std::string::npos) continue;
        std::string k = kv.substr(0, e), v = kv.substr(e + 1);
        if (k == "uv") m.uv = std::stoi(v);
        else if (k == "ubo") sb(v, m.ubo_set, m.ubo_bind);
        else if (k == "p0") m.p0 = uint32_t(std::stoul(v));
        else if (k == "idx") m.idx = uint32_t(std::stoul(v));
        else if (k == "ssbo") sb(v, m.ssbo_set, m.ssbo_bind);
        else if (k == "ind") sb(v, m.ind_set, m.ind_bind);
        else if (k == "uvs") {
          Field f;
          ok = parse_field(v, f, 2);
          m.uvs_kind = f.kind;
          m.uvs_set = f.set, m.uvs_bind = f.binding, m.uvs_off = f.off, m.uvs_k = f.value;
        } else if (k == "alb") ok = parse_field(v, m.f[0], 3);
        else if (k == "rough") ok = parse_field(v, m.f[1], 1);
        else if (k == "metal") ok = parse_field(v, m.f[2], 1);
        else if (k == "refl") ok = parse_field(v, m.f[3], 1);
        else if (k == "albk") ok = parse_field(v, m.albk, 1);
      }
    } catch (...) {
      ok = false;
    }
    if (!ok) {
      ++bad;
      continue;
    }
    t[std::stoull(hash, nullptr, 16)] = m;
  }
  g_table.swap(t);
  log("[hitmat] %zu G-buffer fragment shaders in %s (%u lines not understood)", g_table.size(), path.c_str(), bad);
}

bool read_bound(VkCommandBuffer cmd, uint32_t set, uint32_t binding, VkDeviceSize extra, void* dst, size_t n) {
  Bg3Binding b;
  if (!query_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, set, binding, 0, &b) || !b.buffer) return false;
  if (b.range != VK_WHOLE_SIZE && extra + n > b.range) return false;
  std::shared_lock<std::shared_mutex> lock(tracker().mu);
  auto bm = tracker().buffer_mem.find(b.buffer);
  if (bm == tracker().buffer_mem.end()) return false;
  auto mp = tracker().mapped.find(bm->second.first);
  if (mp == tracker().mapped.end() || !mp->second) return false;
  std::memcpy(dst, mp->second + bm->second.second + b.offset + extra, n);
  return true;
}

bool finite(const float* f, size_t n) {
  for (size_t i = 0; i < n; ++i)
    if (!(f[i] > -1e30f && f[i] < 1e30f)) return false;
  return true;
}

uint32_t family_of(uint64_t fs) {
  auto it = g_family.find(fs);
  if (it != g_family.end()) return it->second;
  if (g_family_fs.size() >= kFamCount) return kFamCount - 1;   // the last bucket collects the rest
  const uint32_t id = uint32_t(g_family_fs.size());
  g_family_fs.push_back(fs);
  g_family[fs] = id;
  return id;
}

int view_req(Frame& fr, VkImageView v, VkImageLayout l, bool array) {
  for (size_t i = 0; i < fr.views.size(); ++i)
    if (fr.views[i].view == v) return int(i);
  fr.views.push_back({v, l, array});
  return int(fr.views.size() - 1);
}

// The record of one G-buffer draw (false: the reason in g_st.rec_fail).
bool make_record(VkCommandBuffer cmd, const CmdState* s, const MatFs& m, Frame& fr, PendingRec& out) {
  PendingRec p{};
  p.view_ind = -1;
  for (int& v : p.view_f) v = -1;
  MatRec& r = p.r;
  r.kind = m.kind;
  r.fs_lo = uint32_t(s->fs);
  r.albk[0] = r.albk[1] = r.albk[2] = r.albk[3] = 1.0f;
  if (m.kind == 1) {
    uint32_t idx = 0;
    if (!read_bound(cmd, m.ubo_set, m.ubo_bind, m.p0, r.p0, 32) || !read_bound(cmd, m.ubo_set, m.ubo_bind, m.idx, &idx, 4)) {
      ++g_st.rec_fail[1];
      return false;
    }
    if (idx > (1u << 20) || !read_bound(cmd, m.ssbo_set, m.ssbo_bind, VkDeviceSize(idx) * 128, r.t, 128)) {
      ++g_st.rec_fail[2];
      return false;
    }
    Bg3Binding ib;
    if (!query_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m.ind_set, m.ind_bind, 0, &ib) || !ib.view) {
      ++g_st.rec_fail[0];
      return false;
    }
    p.view_ind = view_req(fr, ib.view, ib.layout, false);
  } else if (m.kind == 2) {
    r.p0[0] = r.p0[1] = 1.0f;
    if (m.uvs_kind == 3) r.p0[0] = r.p0[1] = m.uvs_k;
    else if (m.uvs_kind == 4 && !read_bound(cmd, m.uvs_set, m.uvs_bind, m.uvs_off, r.p0, 8)) {
      ++g_st.rec_fail[1];
      return false;
    }
  }
  for (int i = 0; i < 4; ++i) {
    const Field& f = m.f[i];
    uint32_t* o = r.f[i];
    if (f.kind == 1 || f.kind == 2) {
      if (uint32_t(f.kind) != m.kind) continue;   // the generator never mixes families
      Bg3Binding b;
      if (!query_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, f.set, f.binding, 0, &b) || !b.view) {
        ++g_st.rec_fail[0];
        continue;   // this field unknown; the others stand
      }
      p.view_f[i] = view_req(fr, b.view, b.layout, f.kind == 1);
      o[0] = (f.kind == 1 ? kSrcVt : kSrcTex) | (uint32_t(f.swz[0]) << 4) | (uint32_t(f.swz[1]) << 6) |
             (uint32_t(f.swz[2]) << 8) | (uint32_t(f.ich) << 12) | (uint32_t(f.col) << 16);
    } else if (f.kind == 3 || f.kind == 4) {
      float v = f.value;
      if (f.kind == 4 && !read_bound(cmd, f.set, f.binding, f.off, &v, 4)) {
        ++g_st.rec_fail[1];
        continue;
      }
      if (!finite(&v, 1)) {
        ++g_st.rec_fail[3];
        continue;
      }
      o[0] = kSrcConst;
      std::memcpy(&o[2], &v, 4);
    } else {
      continue;
    }
    r.known |= 1u << i;
  }
  if (m.albk.kind == 3) {
    r.albk[0] = r.albk[1] = r.albk[2] = m.albk.value;
  } else if (m.albk.kind == 4) {
    float k[3] = {1, 1, 1};
    if (!read_bound(cmd, m.albk.set, m.albk.binding, m.albk.off, k, size_t(m.albk.n == 3 ? 12 : 4))) {
      ++g_st.rec_fail[1];
      r.known &= ~kHmAlbedo;
      r.f[0][0] = kSrcNone;
    } else {
      r.albk[0] = k[0];
      r.albk[1] = m.albk.n == 3 ? k[1] : k[0];
      r.albk[2] = m.albk.n == 3 ? k[2] : k[0];
    }
  }
  if (!finite(r.p0, 8) || !finite(r.t, 32) || !finite(r.albk, 4)) {
    ++g_st.rec_fail[3];
    return false;
  }
  if (!r.known) return false;
  out = p;
  return true;
}

bool ensure_pipeline() {
  Bg3ShaderBlob blob{};
  if (!shader_get("rt/uv_scatter.hlsl", "main", "cs_6_6", &blob)) return g_pipe != VK_NULL_HANDLE;
  if (g_pipe && blob.generation == g_pipe_gen) return true;
  DeviceData* dd = g_dd;
  if (!g_dsl) {
    VkDescriptorSetLayoutBinding b[4];
    for (uint32_t i = 0; i < 4; ++i) b[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo dci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    dci.bindingCount = 4;
    dci.pBindings = b;
    if (dd->vk.CreateDescriptorSetLayout(dd->device, &dci, nullptr, &g_dsl) != VK_SUCCESS) return false;
    VkPushConstantRange pc{VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
    VkPipelineLayoutCreateInfo pci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pci.setLayoutCount = 1;
    pci.pSetLayouts = &g_dsl;
    pci.pushConstantRangeCount = 1;
    pci.pPushConstantRanges = &pc;
    if (dd->vk.CreatePipelineLayout(dd->device, &pci, nullptr, &g_pl) != VK_SUCCESS) return false;
  }
  VkShaderModuleCreateInfo mci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  mci.codeSize = blob.size_bytes;
  mci.pCode = blob.code;
  VkShaderModule mod = VK_NULL_HANDLE;
  if (dd->vk.CreateShaderModule(dd->device, &mci, nullptr, &mod) != VK_SUCCESS) return g_pipe != VK_NULL_HANDLE;
  VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main"};
  ci.layout = g_pl;
  VkPipeline p = VK_NULL_HANDLE;
  VkResult r = dd->vk.CreateComputePipelines(dd->device, VK_NULL_HANDLE, 1, &ci, nullptr, &p);
  dd->vk.DestroyShaderModule(dd->device, mod, nullptr);
  if (r != VK_SUCCESS) {
    log("[hitmat] uv_scatter pipeline creation failed (%d)", r);
    return g_pipe != VK_NULL_HANDLE;
  }
  if (g_pipe) defer_destroy(BG3_DEFER_PIPELINE, uint64_t(g_pipe));
  g_pipe = p;
  g_pipe_gen = blob.generation;
  return true;
}

// The per-slot regions and the capture buffer, on first use. Caller holds g_mu.
bool ensure_buffers() {
  if (g_ready) return true;
  if (g_failed || !g_dd) return false;
  auto up = [](VkDeviceSize v) { return (v + 255) / 256 * 256; };
  g_max_inst = uint32_t((std::max)(cfg_int("rt_hitmat_instances", 262144), 4096));
  g_rec_region = up(16 + VkDeviceSize(kMaxRecs) * kRecBytes);
  g_inst_region = up(VkDeviceSize(g_max_inst) * 8);
  g_stats_region = up(VkDeviceSize(4 * kFamCount + kQaCount) * 4);
  g_job_region = up(VkDeviceSize(kMaxJobs) * 16);
  g_slot_bytes = g_rec_region + g_inst_region + g_stats_region + g_job_region;
  const VkDeviceSize xfb_mb = VkDeviceSize((std::max)(cfg_int("rt_hitmat_xfb_mb", 128), 8));
  bool ok = make_buf(g_slots, g_slot_bytes * kSceneSlots, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true,
                     "per-slot records, instance entries, statistics, jobs") &&
            make_buf(g_xfb, xfb_mb << 20,
                     VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false,
                     "G-buffer UV capture");
  p_bind = reinterpret_cast<PFN_vkCmdBindTransformFeedbackBuffersEXT>(
      g_dd->gdpa(g_dd->device, "vkCmdBindTransformFeedbackBuffersEXT"));
  p_begin = reinterpret_cast<PFN_vkCmdBeginTransformFeedbackEXT>(g_dd->gdpa(g_dd->device, "vkCmdBeginTransformFeedbackEXT"));
  p_end = reinterpret_cast<PFN_vkCmdEndTransformFeedbackEXT>(g_dd->gdpa(g_dd->device, "vkCmdEndTransformFeedbackEXT"));
  ok = ok && p_bind && p_begin && p_end;
  if (!ok) {
    g_failed = true;
    free_buf(g_slots);
    free_buf(g_xfb);
    log("[hitmat] setup failed: hit materials off");
    return false;
  }
  for (uint32_t s = 0; s < kSceneSlots; ++s) {
    std::memset(g_slots.map + off_rec(s), 0, 16);
    std::memset(g_slots.map + off_inst(s), 0xff, size_t(g_inst_region));
    std::memset(g_slots.map + off_stats(s), 0, size_t(g_stats_region));
  }
  g_ready = true;
  return true;
}

void barrier(VkCommandBuffer cmd, VkPipelineStageFlags src, VkAccessFlags sa, VkPipelineStageFlags dst, VkAccessFlags da) {
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = sa;
  mb.dstAccessMask = da;
  g_dd->vk.CmdPipelineBarrier(cmd, src, dst, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

void log_stats() {
  Stats& st = g_st;
  ++st.frames;
  const int every = (std::max)(cfg_int("rt_timing_frames", 300), 30);
  if (st.frames < uint64_t(every)) return;
  const double f = 1.0 / double(st.frames);
  log("[hitmat] last %llu frames (%llu joined): per frame %.1f G-buffer draws, %.1f joined to TLAS instances, %.1f not "
      "joined, %.1f not in the TLAS, %.1f indirect, %.1f FS not in material_fs.txt, %.1f records; record fields "
      "dropped: no view %.1f, UBO %.1f, SSBO %.1f, non-finite %.1f, records full %.1f, views full %.1f; UV: prepass "
      "(alpha) %.1f, cached mesh %.1f, captured %.1f, not captured: XFB %.1f, topology %.1f, UV location %.1f, count %.1f, "
      "buffer %.1f, jobs %.1f",
      (unsigned long long)st.frames, (unsigned long long)st.frames_joined, st.draws * f, st.joined * f, st.unjoined * f,
      st.not_in_tlas * f, st.indirect * f, st.no_table * f, st.recs * f, st.rec_fail[0] * f, st.rec_fail[1] * f,
      st.rec_fail[2] * f, st.rec_fail[3] * f, st.rec_fail[4] * f, st.rec_fail[5] * f, st.uv_prepass * f,
      st.uv_cached * f, st.uv_captured * f, st.uv_fail[0] * f, st.uv_fail[1] * f, st.uv_fail[2] * f, st.uv_fail[3] * f,
      st.uv_fail[4] * f, st.uv_fail[5] * f);
  // Hit coverage (sampled hits of the consumers, rt_hitmat_stats=1): per family.
  uint64_t hits = 0, kn[3] = {};
  std::vector<std::pair<uint64_t, uint32_t>> order;
  for (uint32_t i = 0; i < kFamCount; ++i) {
    hits += st.fam[i][0];
    for (int k = 0; k < 3; ++k) kn[k] += st.fam[i][k + 1];
    if (st.fam[i][0]) order.push_back({st.fam[i][0], i});
  }
  if (hits) {
    std::sort(order.rbegin(), order.rend());
    log("[hitmat] coverage: hits %llu, known albedo %.1f%%, roughness %.1f%%, metalness %.1f%%; unjoined %.1f%%, "
        "legacy %.1f%%",
        (unsigned long long)hits, 100.0 * kn[0] / hits, 100.0 * kn[1] / hits, 100.0 * kn[2] / hits,
        100.0 * st.fam[0][0] / hits, 100.0 * st.fam[1][0] / hits);
    const Bg3AssetDbApi* db = assetdb_api();
    std::string list;
    for (size_t i = 0; i < order.size() && i < 40; ++i) {
      const uint32_t id = order[i].second;
      const uint64_t fs = id < g_family_fs.size() ? g_family_fs[id] : 0;
      std::string name = id == 0 ? "(unjoined)" : id == 1 ? "(legacy)" : id == kFamCount - 1 ? "(other)" : "";
      Bg3MaterialInfo mi{};
      if (name.empty() && db && db->material && db->material(fs, &mi) && mi.template_name) name = mi.template_name;
      char b[192];
      snprintf(b, sizeof b, "%s%016llx %s %llu %.1f/%.1f/%.1f", i ? "; " : "", (unsigned long long)fs,
               name.empty() ? "?" : name.c_str(), (unsigned long long)st.fam[id][0], 100.0 * st.fam[id][1] / st.fam[id][0],
               100.0 * st.fam[id][2] / st.fam[id][0], 100.0 * st.fam[id][3] / st.fam[id][0]);
      list += b;
    }
    log("[hitmat] coverage by family (fs template hits albedo/roughness/metalness %%): %s", list.c_str());
  }
  // The on-screen self-check sums (rt_reflections.hlsl, kQa*), raw: scripts/qa/coverage.py derives the verdict.
  std::string qa;
  for (uint32_t i = 0; i < kQaCount; ++i) {
    char b[32];
    snprintf(b, sizeof b, "%s%llu", i ? " " : "", (unsigned long long)st.qa[i]);
    qa += b;
  }
  log("[hitmat] selfcheck %s", qa.c_str());
  st = Stats{};
}

}  // namespace

void hitmat_device_created(DeviceData* dd) {
  if (g_dd || !cfg_int("rt", 0)) return;
  g_dd = dd;
}

void hitmat_device_destroyed(DeviceData* dd) {
  if (dd != g_dd) return;
  std::lock_guard<std::mutex> lock(g_mu);
  free_buf(g_slots);
  free_buf(g_xfb);
  if (g_pipe) defer_destroy(BG3_DEFER_PIPELINE, uint64_t(g_pipe));
  if (g_pl) defer_destroy(BG3_DEFER_PIPELINE_LAYOUT, uint64_t(g_pl));
  if (g_dsl) defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, uint64_t(g_dsl));
  g_pipe = VK_NULL_HANDLE;
  g_pl = VK_NULL_HANDLE;
  g_dsl = VK_NULL_HANDLE;
  g_ready = g_failed = false;
  g_filled.clear();
  g_fr = Frame{};
  g_dd = nullptr;
}

bool hitmat_wanted() { return g_dd && !g_failed && cfg_int("rt_hitmat", 0) != 0 && cfg_int("rt_alpha", 0) != 0; }

// Called by alpha_commit with alphatest.cpp's lock held (lock order: alpha, then ours).
bool hitmat_on_commit(uint32_t slot, HitmatSlotBufs* out) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (!hitmat_wanted() || slot >= kSceneSlots || !ensure_buffers()) return false;
  load_table();
  // The statistics of the slot's last use (the GPU finished it: the slot is retired).
  const uint32_t* sp = reinterpret_cast<const uint32_t*>(g_slots.map + off_stats(slot));
  for (uint32_t i = 0; i < kFamCount; ++i)
    for (int k = 0; k < 4; ++k) g_st.fam[i][k] += sp[i * 4 + k];
  for (uint32_t i = 0; i < kQaCount; ++i) g_st.qa[i] += sp[4 * kFamCount + i];
  std::memset(g_slots.map + off_stats(slot), 0, size_t(g_stats_region));
  // This build's instances: no material until the G-buffer pass joins them.
  BvhJoinFrame j;
  uint32_t n_inst = 0;
  if (bvh_join_frame(&j) && j.slot == slot && j.frame == frame_index()) n_inst = (std::min)(j.n_inst, g_max_inst);
  uint32_t* hdr = reinterpret_cast<uint32_t*>(g_slots.map + off_rec(slot));
  hdr[0] = 0;
  hdr[1] = n_inst;
  hdr[2] = uint32_t(frame_index());
  hdr[3] = 1;   // hit materials on: the shaders may write the statistics
  std::memset(g_slots.map + off_inst(slot), 0xff, size_t(n_inst) * 8);
  for (uint32_t i = 0; i < n_inst; ++i)   // family 0: unjoined
    reinterpret_cast<uint32_t*>(g_slots.map + off_inst(slot))[i * 2 + 1] = 0;
  out->buf = g_slots.b;
  out->rec_off = off_rec(slot);
  out->rec_bytes = g_rec_region;
  out->inst_off = off_inst(slot);
  out->inst_bytes = g_inst_region;
  out->stats_off = off_stats(slot);
  out->stats_bytes = g_stats_region;
  log_stats();
  return true;
}

void hitmat_pass_before(VkCommandBuffer cmd, CmdState* s) {
  s->hm = nullptr;
  if (!g_dd || !geo_is_gbuffer_pass(s->pass) || !hitmat_wanted()) return;
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_ready) return;
  Frame& fr = g_fr;
  const uint64_t now = frame_index();
  if (fr.frame != now) {
    fr = Frame{};
    fr.frame = now;
    // This frame's split build (the G-buffer follows the prepass in the same frame).
    fr.ok = bvh_join_frame(&fr.join) && fr.join.frame == now && fr.join.stride == 24;
    if (fr.ok) {
      for (uint32_t i = 0; i < fr.join.draws.size(); ++i) fr.by_key[fr.join.draws[i].geom_key].push_back(i);
      ++g_st.frames_joined;
      fr.pipe_ok = ensure_pipeline();   // no scatter pipeline: no UV captures (counted as "no XFB pipeline")
    }
  }
  if (!fr.ok) return;
  // Last frame's scatter read the capture buffer: done before this pass writes it.
  barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT, 0);
  s->hm = &fr;
}

void hitmat_pre_draw(VkCommandBuffer cmd, CmdState* s, const DrawArgs& args) {
  std::lock_guard<std::mutex> lock(g_mu);
  Frame& fr = g_fr;
  if (s->hm != &fr || !fr.ok) return;
  ++g_st.draws;
  if (args.kind > 1) {
    ++g_st.indirect;
    return;
  }
  const uint64_t gk = draw_geom_key(s, args);
  const uint32_t o = fr.occ[gk]++;
  auto it = fr.by_key.find(gk);
  if (it == fr.by_key.end() || o >= it->second.size()) {
    ++g_st.unjoined;
    return;
  }
  const BvhJoinDraw& jd = fr.join.draws[it->second[o]];
  if (!jd.inst_count) {
    ++g_st.not_in_tlas;
    return;
  }
  ++g_st.joined;
  const uint32_t fam = family_of(s->fs);
  // The record.
  uint32_t rec = kNone;
  auto mt = g_table.find(s->fs);
  const MatFs* m = mt != g_table.end() ? &mt->second : nullptr;
  if (!m) ++g_st.no_table;
  if (m && fr.recs.size() + fr.recs_written < kMaxRecs) {
    PendingRec p;
    if (make_record(cmd, s, *m, fr, p)) {
      rec = uint32_t(fr.recs.size());
      fr.recs.push_back(p);
      ++g_st.recs;
    }
  } else if (m) {
    ++g_st.rec_fail[4];
  }
  // The UV at the instance's vertices.
  bool uv = false;
  if (rec != kNone) {
    const bool need_uv = (m->kind == 1 || m->kind == 2);
    if (!need_uv) {
      uv = true;   // constants only
    } else if (jd.alpha) {
      uv = true;   // the prepass captured it (alpha-tested draws)
      ++g_st.uv_prepass;
    } else if (jd.cached) {
      auto f = g_filled.find(jd.mesh_key);
      if (f != g_filled.end() && f->second.gen == jd.mesh_gen) {
        const int st = bvh_rec_state(f->second.rec);
        if (st == 1 || f->second.frame == fr.frame) uv = true, ++g_st.uv_cached;
        else if (st != 0) g_filled.erase(f);   // its command buffer was dropped: fill again
      }
      for (const auto& fn : fr.filled_now)
        if (fn.first == jd.mesh_key && fn.second == jd.mesh_gen) uv = true;
    }
    if (!uv && need_uv) {
      // Capture this draw's UV (transform feedback of the G-buffer VS's UV output).
      int loc = -1;
      VkPrimitiveTopology topo = VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
      const uint64_t prims = uint64_t(args.count / 3) * args.instances;
      const uint32_t want = jd.cached ? jd.tris_per_instance * jd.instances : jd.prims;
      const VkDeviceSize bytes = prims * 72;
      int why = -1;
      if (s->geo || !fr.pipe_ok || !geo_pipe_capture(s->pipe[0], &loc, &topo)) why = 0;
      else if (topo != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) why = 1;
      else if (loc != m->uv) why = 2;
      else if (prims != want || !prims) why = 3;
      else if (fr.cursor + bytes > g_xfb.size) why = 4;
      else if (fr.jobs.size() >= kMaxJobs) why = 5;
      if (why >= 0) {
        ++g_st.uv_fail[why];
      } else {
        VkDeviceSize off = fr.cursor;
        p_bind(cmd, 0, 1, &g_xfb.b, &off, &bytes);
        p_begin(cmd, 0, 0, nullptr, nullptr);
        fr.xfb_active = true;
        fr.cursor += bytes;
        const uint32_t verts = jd.cached ? jd.tris_per_instance * 3 : jd.prims * 3;
        fr.jobs.push_back({uint32_t(off), uint32_t(jd.cached ? jd.pool_off : jd.soup_off), verts, jd.cached ? 1u : 0u});
        if (jd.cached) fr.filled_now.push_back({jd.mesh_key, jd.mesh_gen});
        uv = true;
        ++g_st.uv_captured;
      }
    }
  }
  // Entries for the draw's TLAS instances (written at the pass end).
  const uint32_t y = fam | ((uv ? kInstUv : 0u) << 16);
  for (uint32_t i = 0; i < jd.inst_count; ++i) {
    fr.entries.push_back({jd.inst_first + i, y});
    fr.entry_rec.push_back(rec);
  }
}

void hitmat_post_draw(VkCommandBuffer cmd, CmdState* s) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (s->hm != &g_fr || !g_fr.xfb_active) return;
  p_end(cmd, 0, 0, nullptr, nullptr);
  g_fr.xfb_active = false;
}

void hitmat_pass_ending(VkCommandBuffer cmd, CmdState* s) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (s->hm != &g_fr || !g_fr.xfb_active) return;
  p_end(cmd, 0, 0, nullptr, nullptr);
  g_fr.xfb_active = false;
}

void hitmat_pass_after(VkCommandBuffer cmd, CmdState* s) {
  std::unique_lock<std::mutex> lock(g_mu);
  Frame& fr = g_fr;
  if (s->hm != &fr) return;
  s->hm = nullptr;
  if (!fr.ok) return;
  const uint32_t slot = fr.join.slot;
  const uint64_t frame = fr.frame;
  // The views into the slot's set (after the alpha path's own). alphatest.cpp's lock is
  // taken without ours (alpha_commit holds it while it calls hitmat_on_commit).
  std::vector<AlphaViewReq> req;
  req.reserve(fr.views.size());
  for (const ViewReq& v : fr.views) req.push_back({v.view, v.layout, v.array});
  std::vector<uint32_t> idx;
  lock.unlock();
  const bool views_ok = !req.empty() && alpha_append_views(slot, req, &idx) && idx.size() == req.size();
  lock.lock();
  if (g_fr.frame != frame || !g_ready) return;
  // Records: view slots resolved; a field whose view did not fit is dropped (counted).
  uint32_t* hdr = reinterpret_cast<uint32_t*>(g_slots.map + off_rec(slot));
  MatRec* out = reinterpret_cast<MatRec*>(g_slots.map + off_rec(slot) + 16);
  std::vector<uint32_t> remap(fr.recs.size(), kNone);
  for (size_t i = 0; i < fr.recs.size(); ++i) {
    PendingRec& p = fr.recs[i];
    MatRec& r = p.r;
    auto slot_of = [&](int v) -> uint32_t { return v >= 0 && views_ok ? idx[size_t(v)] : 0u; };
    if (p.view_ind >= 0) {
      r.ind = slot_of(p.view_ind);
      if (!r.ind) {   // no indirection texture: none of the vt fields can be read
        for (int k = 0; k < 4; ++k)
          if ((r.f[k][0] & 15u) == kSrcVt) r.f[k][0] = kSrcNone, r.known &= ~(1u << k);
        ++g_st.rec_fail[5];
      }
    }
    for (int k = 0; k < 4; ++k) {
      if (p.view_f[k] < 0) continue;
      r.f[k][1] = slot_of(p.view_f[k]);
      if (!r.f[k][1]) r.f[k][0] = kSrcNone, r.known &= ~(1u << k), ++g_st.rec_fail[5];
    }
    if (!r.known) continue;
    const uint32_t at = fr.recs_written;
    if (at >= kMaxRecs) break;
    out[at] = r;
    remap[i] = at;
    ++fr.recs_written;
  }
  hdr[0] = fr.recs_written;
  // Instance entries.
  uint32_t* ent = reinterpret_cast<uint32_t*>(g_slots.map + off_inst(slot));
  const uint32_t n_inst = hdr[1];
  for (size_t i = 0; i < fr.entries.size(); ++i) {
    const uint32_t inst = fr.entries[i].first;
    if (inst >= n_inst) continue;
    const uint32_t r = fr.entry_rec[i];
    ent[inst * 2] = r != kNone && r < remap.size() ? remap[r] : kNone;
    ent[inst * 2 + 1] = fr.entries[i].second;
  }
  fr.recs.clear();
  fr.views.clear();
  fr.entries.clear();
  fr.entry_rec.clear();
  // The UV scatter: G-buffer capture -> the scene's UV slots.
  if (!fr.jobs.empty()) {
    std::memcpy(g_slots.map + off_jobs(slot), fr.jobs.data(), fr.jobs.size() * sizeof(Job));
    barrier(cmd, VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    VkDescriptorBufferInfo src{g_xfb.b, 0, g_xfb.size}, soup{fr.join.soup, 0, fr.join.soup_size},
        pool{fr.join.pool ? fr.join.pool : fr.join.soup, 0, fr.join.pool ? fr.join.pool_size : fr.join.soup_size},
        jobs{g_slots.b, off_jobs(slot), g_job_region};
    const VkDescriptorBufferInfo* infos[4] = {&src, &soup, &pool, &jobs};
    VkWriteDescriptorSet w[4];
    for (uint32_t i = 0; i < 4; ++i) {
      w[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      w[i].dstBinding = i;
      w[i].descriptorCount = 1;
      w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      w[i].pBufferInfo = infos[i];
    }
    const VkDeviceFuncs& vk = g_dd->vk;
    vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_pipe);
    vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_pl, 0, 4, w);
    uint32_t pc[4] = {uint32_t(fr.jobs.size()), 0, 0, 0};
    vk.CmdPushConstants(cmd, g_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, pc);
    vk.CmdDispatch(cmd, uint32_t(fr.jobs.size()), 1, 1);
    barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
    restore_bindings(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
    if (!fr.cmd_rec_set) {
      fr.cmd_rec = bvh_rec_register(cmd);
      fr.cmd_rec_set = true;
    }
    for (const auto& fn : fr.filled_now) g_filled[fn.first] = Filled{fn.second, fr.cmd_rec, fr.frame};
    fr.jobs.clear();
    fr.filled_now.clear();
  }
}

}  // namespace bg3
