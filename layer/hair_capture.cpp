// rt_hair_capture: the layer side of the runtime hair capture (docs/RTX_HAIR_ROADMAP.md 2c, hair_capture.h).
#include "host.h"
#include "hair_capture.h"
#include "assetdb.h"
#include "../common/hair_capture.h"
#include "../common/hair_groom_asset.h"
#include "../common/hair_primary.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bg3::hair_capture {
namespace {
namespace hc = bg3::hair_capture;

constexpr uint32_t kJobs = 2;                                // staging buffers in flight at most
constexpr VkDeviceSize kMaxStaging = VkDeviceSize(64) << 20; // one capture's copies (largest style ~11 MB)
constexpr uint64_t kDiskCap = uint64_t(512) << 20;           // <assetdb>\hair_capture
constexpr size_t kTriedMax = 8192, kStylesMax = 1024;
constexpr size_t kFirstBytesMax = size_t(128) << 20;         // first-seen corner positions kept for the stretch

bool g_on = false;

struct Staging { VkBuffer b = VK_NULL_HANDLE; VkDeviceMemory m = VK_NULL_HANDLE; uint8_t* map = nullptr; };
struct Job {
  Staging buf;
  uint64_t frame = 0, rec = 0, material = 0;
  uint32_t count = 0, index_bytes = 0, stride = 0, vk_format = 0;
  hc::MaskInfo mask;
  VkDeviceSize idx_off = 0, soup_off = 0, mask_off = 0, mask_bytes = 0;
  bool used = false, recorded = false;
  // Staged for this frame's record() only.
  bool gpu_indices = false;
  VkBuffer ib = VK_NULL_HANDLE, soup = VK_NULL_HANDLE;
  VkDeviceSize ib_offset = 0, src_soup_off = 0;
  VkImage image = VK_NULL_HANDLE;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  uint32_t layer = 0;
};
Job g_jobs[kJobs];
int g_staged = -1;   // job staged this frame, recorded by record()
uint64_t g_staged_frame = 0;
std::unordered_set<uint64_t> g_tried;          // (index count, mask image id) decided once
std::unordered_set<uint32_t> g_no_mask_counts;

// Worker <-> main.
std::mutex g_mu;
std::condition_variable g_cv;
std::deque<Job> g_queue;              // GPU-done readbacks for the worker
std::vector<Staging> g_free;          // read by the worker: the main thread frees them
std::thread g_worker;
bool g_stop = false;
std::atomic<bool> g_disk_full{false};
std::atomic<uint32_t> g_busy{0};      // jobs queued or being processed by the worker

struct Window { std::atomic<uint64_t> written{0}, placeholder{0}, no_mask{0}, cap{0}, other{0}, kept{0}, bytes{0}, dropped{0}; };
Window g_win;
uint64_t g_win_start = 0;

uint64_t try_key(uint32_t count, uint32_t image_id) { return (uint64_t(count) << 32) | image_id; }

bool tried(uint64_t key) {
  if (g_tried.count(key)) return true;
  if (g_tried.size() >= kTriedMax) g_tried.clear();   // the worker still deduplicates by index id
  g_tried.insert(key);
  return false;
}

std::string capture_dir() {
  std::string db = assetdb_file();
  const size_t s = db.find_last_of("\\/");
  return (s == std::string::npos ? std::string(".") : db.substr(0, s)) + "\\hair_capture";
}

void free_staging(DeviceData* dd, Staging& s) {
  if (s.b) dd->vk.DestroyBuffer(dd->device, s.b, nullptr);
  if (s.m) dd->vk.FreeMemory(dd->device, s.m, nullptr);
  s = Staging{};
}

bool make_staging(DeviceData* dd, VkDeviceSize size, Staging& out) {
  out = Staging{};
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = size;
  bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  if (dd->vk.CreateBuffer(dd->device, &bci, nullptr, &out.b) != VK_SUCCESS) { out.b = VK_NULL_HANDLE; return false; }
  VkMemoryRequirements req;
  dd->vk.GetBufferMemoryRequirements(dd->device, out.b, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  // Cached host memory: the worker reads several MB from it (uncached reads are slow).
  constexpr VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  mai.memoryTypeIndex = find_memory_type(dd, req.memoryTypeBits, host | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
  if (mai.memoryTypeIndex == UINT32_MAX) mai.memoryTypeIndex = find_memory_type(dd, req.memoryTypeBits, host);
  void* map = nullptr;
  if (mai.memoryTypeIndex == UINT32_MAX || dd->vk.AllocateMemory(dd->device, &mai, nullptr, &out.m) != VK_SUCCESS) {
    out.m = VK_NULL_HANDLE; free_staging(dd, out); return false;
  }
  if (dd->vk.BindBufferMemory(dd->device, out.b, out.m, 0) != VK_SUCCESS ||
      dd->vk.MapMemory(dd->device, out.m, 0, VK_WHOLE_SIZE, 0, &map) != VK_SUCCESS || !map) {
    free_staging(dd, out); return false;
  }
  out.map = static_cast<uint8_t*>(map);
  return true;
}

// The index range in the game's host mapping (as hair_groom's join reads it), or nullptr.
const uint8_t* host_indices(VkBuffer ib, VkDeviceSize offset, VkDeviceSize bytes) {
  std::shared_lock lock(tracker().mu);
  const auto bm = tracker().buffer_mem.find(ib);
  if (bm == tracker().buffer_mem.end()) return nullptr;
  const auto mp = tracker().mapped.find(bm->second.first);
  const auto range = tracker().mapped_ranges.find(bm->second.first);
  const uint64_t start = bm->second.second + offset;
  if (mp == tracker().mapped.end() || !mp->second || range == tracker().mapped_ranges.end() || start < range->second.first ||
      start + bytes > range->second.second) return nullptr;
  return mp->second + start;
}

// ---- worker: rebuild, full-mask rule, stretch, encode, write ----
struct Style { hc::MaskInfo mask; uint32_t captures = 0; bool from_disk = false; std::vector<float> first; };
std::unordered_map<uint64_t, Style> g_styles;   // worker only
size_t g_first_bytes = 0;
uint64_t g_dir_bytes = 0;
bool g_dir_scanned = false, g_cap_logged = false;

uint64_t file_size(const std::string& path) {
  WIN32_FILE_ATTRIBUTE_DATA a{};
  if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &a)) return 0;
  return (uint64_t(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
}

void scan_dir(const std::string& dir) {
  if (g_dir_scanned) return;
  g_dir_scanned = true;
  CreateDirectoryA(dir.c_str(), nullptr);
  WIN32_FIND_DATAA fd{};
  HANDLE h = FindFirstFileA((dir + "\\*.hcap").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do g_dir_bytes += (uint64_t(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
  while (FindNextFileA(h, &fd));
  FindClose(h);
}

// An earlier session's capture of this style: its mask size and capture count (header only).
bool read_existing(const std::string& path, hc::MaskInfo& mask, uint32_t& captures) {
  FILE* f = nullptr;
  if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
  uint8_t h[hc::kHeaderBytes] = {};
  const size_t got = fread(h, 1, sizeof h, f);
  fclose(f);
  if (got != sizeof h || std::memcmp(h, "HCAP", 4) != 0) return false;
  uint32_t version = 0;
  std::memcpy(&version, h + 4, 4);
  if (version != hc::kVersion) return false;
  std::memcpy(&mask.width, h + 16, 4);
  std::memcpy(&mask.height, h + 20, 4);
  mask.mips = 2;   // only full masks are ever written
  std::memcpy(&captures, h + hc::kHeaderBytes - 4, 4);
  return true;
}

void process(Job& j) {
  const std::string dir = capture_dir();
  scan_dir(dir);
  hc::Capture c;
  c.mask = j.mask;
  c.material = j.material;
  c.mask_format = j.vk_format == VK_FORMAT_BC3_UNORM_BLOCK || j.vk_format == VK_FORMAT_BC3_SRGB_BLOCK ? hc::kMaskBc3 : hc::kMaskRgba8;
  c.indices.resize(j.count);
  c.corner_pos.resize(size_t(j.count) * 3);
  c.corner_uv.resize(size_t(j.count) * 2);
  const uint8_t* ip = j.buf.map + j.idx_off;
  for (uint32_t i = 0; i < j.count; ++i) {
    if (j.index_bytes == 2) { uint16_t v; std::memcpy(&v, ip + size_t(i) * 2, 2); c.indices[i] = v; }
    else std::memcpy(&c.indices[i], ip + size_t(i) * 4, 4);
  }
  bool finite = true;
  for (uint32_t i = 0; i < j.count; ++i) {
    const uint8_t* corner = j.buf.map + j.soup_off + size_t(i) * j.stride;
    std::memcpy(&c.corner_pos[size_t(i) * 3], corner, 12);
    std::memcpy(&c.corner_uv[size_t(i) * 2], corner + 16, 8);
    for (int k = 0; k < 3; ++k) finite = finite && std::isfinite(c.corner_pos[size_t(i) * 3 + k]);
    for (int k = 0; k < 2; ++k) finite = finite && std::isfinite(c.corner_uv[size_t(i) * 2 + k]);
  }
  c.mask_bytes.assign(j.buf.map + j.mask_off, j.buf.map + j.mask_off + j.mask_bytes);
  {
    std::lock_guard lock(g_mu);   // the staging is read: the main thread frees it
    g_free.push_back(j.buf);
    j.buf = Staging{};
  }
  const uint64_t id = hair_groom::index_content_id([&](uint32_t i) { return c.indices[i]; }, j.count);
  char name[64];
  if (id) snprintf(name, sizeof name, "%016llx", (unsigned long long)id);
  else snprintf(name, sizeof name, "count_%u", j.count);
  const std::string path = dir + "\\" + name + ".hcap";
  uint32_t lo = UINT32_MAX, hi = 0;
  for (uint32_t v : c.indices) { lo = (std::min)(lo, v); hi = (std::max)(hi, v); }
  const uint32_t verts = j.count ? hi - lo + 1 : 0;

  const char* skip = nullptr;
  auto it = g_styles.find(id);
  Style local;
  Style* st = &local;
  if (it != g_styles.end()) st = &it->second;
  else if (g_styles.size() < kStylesMax) st = &g_styles[id];
  if (st->captures == 0 && !st->from_disk) {
    uint32_t captures = 0;
    if (read_existing(path, st->mask, captures)) { st->from_disk = true; st->captures = captures; }
  }
  // The stretch against the style's first capture of this session (1/1/1 for the first).
  if (finite && !st->first.empty() && st->first.size() == c.corner_pos.size())
    c.stretch = hc::stretch(st->first.data(), c.corner_pos.data(), j.count);
  const bool first_capture = st->captures == 0;
  if (!finite) skip = "non-finite soup";
  else if (!hc::full_mask(j.mask, st->mask.width, st->mask.height)) skip = st->from_disk ? "exists, mask smaller" : "mask smaller";
  else if (!first_capture && !hc::upgrade(st->mask, j.mask)) skip = st->from_disk ? "exists" : "not larger";
  if (finite && st->first.empty() && g_first_bytes + c.corner_pos.size() * 4 <= kFirstBytesMax && st != &local) {
    st->first = c.corner_pos;
    g_first_bytes += st->first.size() * 4;
  }
  std::vector<uint8_t> bytes;
  if (!skip) {
    c.captures = st->captures + 1;
    bytes = hc::encode_hcap(c);
    if (bytes.empty()) skip = "encode failed";
  }
  if (!skip) {
    const uint64_t old = file_size(path);
    if (g_dir_bytes - (std::min)(g_dir_bytes, old) + bytes.size() > kDiskCap) {
      skip = "disk cap";
      g_disk_full = true;
      ++g_win.cap;
      if (!g_cap_logged) {
        g_cap_logged = true;
        log("[hair capture] %s holds %.0f MB (cap %llu MB): capturing stopped", dir.c_str(), double(g_dir_bytes) / 1048576.0,
            (unsigned long long)(kDiskCap >> 20));
      }
    } else {
      const std::string tmp = path + "." + std::to_string(GetCurrentProcessId()) + ".tmp";
      if (write_file(tmp, bytes.data(), bytes.size()) && MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        g_dir_bytes = g_dir_bytes - (std::min)(g_dir_bytes, old) + bytes.size();
        st->mask = j.mask;
        st->captures = c.captures;
        ++g_win.written;
        g_win.bytes += bytes.size();
      } else {
        DeleteFileA(tmp.c_str());
        skip = "write failed";
      }
    }
  }
  if (skip && std::strcmp(skip, "disk cap") != 0) ++g_win.kept;
  log("[hair capture] %s: %u indices, %u vertices, mask %ux%u fmt %u mips %u, stretch %.4f/%.4f/%.4f, %llu KB, %s%s%s",
      name, j.count, verts, j.mask.width, j.mask.height, j.vk_format, j.mask.mips, c.stretch.min, c.stretch.median, c.stretch.max,
      (unsigned long long)((bytes.empty() ? j.mask_bytes + VkDeviceSize(j.count) * 24 : bytes.size()) / 1024),
      skip ? "skipped (" : "written", skip ? skip : "", skip ? ")" : "");
}

void worker() {
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
  for (;;) {
    Job j;
    {
      std::unique_lock lock(g_mu);
      g_cv.wait(lock, [] { return g_stop || !g_queue.empty(); });
      if (g_queue.empty()) return;   // stop with nothing left
      j = g_queue.front();
      g_queue.pop_front();
    }
    process(j);
    --g_busy;
  }
}

void start_worker() {
  if (g_worker.joinable()) return;
  g_stop = false;
  g_worker = std::thread(worker);
}

bool supported_format(VkFormat f) {
  return f == VK_FORMAT_BC3_UNORM_BLOCK || f == VK_FORMAT_BC3_SRGB_BLOCK || f == VK_FORMAT_R8G8B8A8_UNORM || f == VK_FORMAT_R8G8B8A8_SRGB;
}
}  // namespace

void begin_capture() { g_on = cfg_int("rt_hair_capture", 0) != 0; }
bool enabled() { return g_on; }

bool ready() {
  if (!g_on || g_disk_full || g_staged >= 0) return false;
  for (const Job& j : g_jobs) if (!j.used) return true;
  return false;
}

void no_mask(uint32_t count) {
  if (g_no_mask_counts.size() < kTriedMax && g_no_mask_counts.insert(count).second) ++g_win.no_mask;
}

void poll(DeviceData* dd, uint64_t now) {
  if (!dd) return;
  {
    std::vector<Staging> done;
    { std::lock_guard lock(g_mu); done.swap(g_free); }
    for (Staging& s : done) free_staging(dd, s);
  }
  for (uint32_t i = 0; i < kJobs; ++i) {
    Job& j = g_jobs[i];
    if (!j.used) continue;
    if (!j.recorded) {
      // Staged but never recorded (the build failed after the decision): nothing was copied.
      if (g_staged == int(i) && g_staged_frame == now) continue;
      if (g_staged == int(i)) g_staged = -1;
      free_staging(dd, j.buf); j = Job{}; ++g_win.dropped;
      continue;
    }
    const auto state = hair_primary::readback_state(j.frame, life_gpu_done(), bvh_rec_submitted(j.rec));
    if (state == hair_primary::ReadbackState::Wait) continue;
    if (state == hair_primary::ReadbackState::Drop) { free_staging(dd, j.buf); j = Job{}; ++g_win.dropped; continue; }
    start_worker();
    ++g_busy;
    {
      std::lock_guard lock(g_mu);
      g_queue.push_back(j);
    }
    g_cv.notify_one();
    j = Job{};   // the worker owns the staging now (returned through g_free)
  }
  if (!g_win_start) g_win_start = now;
  if (now - g_win_start >= 300) {
    const uint64_t w = g_win.written.exchange(0), p = g_win.placeholder.exchange(0), n = g_win.no_mask.exchange(0),
                   c = g_win.cap.exchange(0), o = g_win.other.exchange(0), k = g_win.kept.exchange(0), b = g_win.bytes.exchange(0),
                   d = g_win.dropped.exchange(0);
    if (w || p || n || c || o || k || d)
      log("[hair capture] %llu frames: %llu captured (%.1f MB written), %llu kept (not larger / exists), %llu skipped placeholder, "
          "%llu skipped no mask, %llu skipped cap, %llu skipped other (format, usage, layout, size), %llu dropped, %u in the worker",
          (unsigned long long)(now - g_win_start), (unsigned long long)w, double(b) / 1048576.0, (unsigned long long)k,
          (unsigned long long)p, (unsigned long long)n, (unsigned long long)c, (unsigned long long)o, (unsigned long long)d,
          g_busy.load());
    g_win_start = now;
  }
}

bool want(DeviceData* dd, const Draw& d, uint64_t now) {
  if (!dd || !ready() || !d.mask || !d.mask_id || !d.count || d.count % 3 || (d.index_bytes != 2 && d.index_bytes != 4)) return false;
  VkImage image = VK_NULL_HANDLE;
  VkImageCreateInfo ci{};
  VkImageSubresourceRange range{};
  uint32_t image_id = 0;
  {
    std::shared_lock lock(tracker().mu);
    const auto v = tracker().views.find(d.mask);
    if (v != tracker().views.end()) {
      const auto im = tracker().images.find(v->second.image);
      if (im != tracker().images.end()) { image = v->second.image; ci = im->second.ci; image_id = im->second.id; range = v->second.range; }
    }
  }
  if (!image || image_id != d.mask_id) { no_mask(d.count); return false; }
  const uint64_t key = try_key(d.count, image_id);
  if (g_tried.count(key)) return false;
  const uint32_t levels = range.levelCount == VK_REMAINING_MIP_LEVELS ? ci.mipLevels - (std::min)(ci.mipLevels, range.baseMipLevel)
                                                                      : range.levelCount;
  hc::MaskInfo m{ci.extent.width, ci.extent.height, (std::min)(levels, ci.mipLevels), uint32_t(ci.format)};
  // A view that starts below mip 0 has no resident full level: as a streaming placeholder.
  if (range.baseMipLevel != 0 || !hc::full_mask(m, 0, 0)) { tried(key); ++g_win.placeholder; return false; }
  const bool layout_ok = d.mask_layout != VK_IMAGE_LAYOUT_UNDEFINED && d.mask_layout != VK_IMAGE_LAYOUT_PREINITIALIZED;
  if (!supported_format(ci.format) || !(ci.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) || !layout_ok || ci.imageType != VK_IMAGE_TYPE_2D ||
      ci.samples != VK_SAMPLE_COUNT_1_BIT) {
    tried(key); ++g_win.other;
    static uint32_t logged = 0;
    if (logged++ < 8)
      log("[hair capture] %u indices: mask image %u not copyable (format %d, usage 0x%x, layout %d): skipped", d.count, image_id,
          int(ci.format), unsigned(ci.usage), int(d.mask_layout));
    return false;
  }
  const VkDeviceSize idx_bytes = VkDeviceSize(d.count) * d.index_bytes;
  const VkDeviceSize ib_size = geo_buffer_size(d.ib);
  const bool gpu_indices = (geo_buffer_usage(d.ib) & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) != 0;
  const uint8_t* host = gpu_indices ? nullptr : host_indices(d.ib, d.ib_offset, idx_bytes);
  const VkDeviceSize soup_bytes = VkDeviceSize(d.count) * d.stride;
  const VkDeviceSize mask_bytes = (ci.format == VK_FORMAT_BC3_UNORM_BLOCK || ci.format == VK_FORMAT_BC3_SRGB_BLOCK)
                                      ? hc::bc3_bytes(m.width, m.height) : VkDeviceSize(m.width) * m.height * 4;
  auto up = [](VkDeviceSize v) { return (v + 15) / 16 * 16; };
  const VkDeviceSize total = up(idx_bytes) + up(soup_bytes) + mask_bytes;
  if (!d.ib || !ib_size || d.ib_offset > ib_size || idx_bytes > ib_size - d.ib_offset || (!gpu_indices && !host) || d.stride < 24 ||
      d.soup_off > d.soup_size || soup_bytes > d.soup_size - d.soup_off || total > kMaxStaging) {
    tried(key); ++g_win.other;
    return false;
  }
  int slot = -1;
  for (uint32_t i = 0; i < kJobs; ++i) if (!g_jobs[i].used) { slot = int(i); break; }
  if (slot < 0) return false;
  Job& j = g_jobs[slot];
  j = Job{};
  if (!make_staging(dd, total, j.buf)) { j = Job{}; return false; }   // retried on a later frame
  tried(key);
  j.used = true;
  j.frame = now;
  j.count = d.count; j.index_bytes = d.index_bytes; j.stride = d.stride; j.vk_format = uint32_t(ci.format);
  j.mask = m; j.material = d.material;
  j.idx_off = 0; j.soup_off = up(idx_bytes); j.mask_off = j.soup_off + up(soup_bytes); j.mask_bytes = mask_bytes;
  j.gpu_indices = gpu_indices;
  if (!gpu_indices) std::memcpy(j.buf.map, host, size_t(idx_bytes));   // the game's mapped index data, this frame
  j.ib = d.ib; j.ib_offset = d.ib_offset; j.soup = d.soup; j.src_soup_off = d.soup_off;
  j.image = image; j.layout = d.mask_layout; j.layer = range.baseArrayLayer;
  g_staged = slot;
  g_staged_frame = now;
  return true;
}

void record(DeviceData* dd, VkCommandBuffer cmd, uint64_t now, uint64_t rec) {
  if (g_staged < 0 || !dd) return;
  Job& j = g_jobs[g_staged];
  g_staged = -1;
  if (!j.used || j.recorded || g_staged_frame != now) return;
  const auto& vk = dd->vk;
  const bool transition = j.layout != VK_IMAGE_LAYOUT_GENERAL && j.layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  VkImageMemoryBarrier ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  ib.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  ib.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  ib.oldLayout = j.layout;
  ib.newLayout = transition ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : j.layout;
  ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  ib.image = j.image;
  ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, j.layer, 1};
  // The soup's conversion (compute) and the game's index / texture uploads before the copies.
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr,
                        transition ? 1u : 0u, &ib);
  if (j.gpu_indices) {
    const VkBufferCopy c{j.ib_offset, j.idx_off, VkDeviceSize(j.count) * j.index_bytes};
    vk.CmdCopyBuffer(cmd, j.ib, j.buf.b, 1, &c);
  }
  const VkBufferCopy s{j.src_soup_off, j.soup_off, VkDeviceSize(j.count) * j.stride};
  vk.CmdCopyBuffer(cmd, j.soup, j.buf.b, 1, &s);
  VkBufferImageCopy r{};
  r.bufferOffset = j.mask_off;
  r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, j.layer, 1};
  r.imageExtent = {j.mask.width, j.mask.height, 1};
  vk.CmdCopyImageToBuffer(cmd, j.image, transition ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : j.layout, j.buf.b, 1, &r);
  // Back to the game's layout; the staging to the host.
  std::swap(ib.oldLayout, ib.newLayout);
  ib.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  ib.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  VkMemoryBarrier hb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, nullptr, 0, nullptr);
  if (transition)
    vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &ib);
  j.rec = rec;
  j.frame = now;
  j.recorded = true;
}

void device_destroyed(DeviceData* dd) {
  {
    std::lock_guard lock(g_mu);
    g_stop = true;
  }
  g_cv.notify_all();
  if (g_worker.joinable()) g_worker.join();   // finishes the queued captures first
  if (!dd) return;
  std::vector<Staging> done;
  { std::lock_guard lock(g_mu); done.swap(g_free); g_stop = false; }
  for (Staging& s : done) free_staging(dd, s);
  for (Job& j : g_jobs) { free_staging(dd, j.buf); j = Job{}; }
  g_staged = -1;
  g_tried.clear();
  g_no_mask_counts.clear();
}

}  // namespace bg3::hair_capture
