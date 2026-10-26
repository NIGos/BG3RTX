// Render-target readback for the tracer. While a trace records, every pass's
// attachments are copied (in the game's own command buffer, right after the
// pass) into host-visible buffers. When the traced frame is written, the
// copies belonging to it are decoded and saved as PNG previews plus exact
// per-channel statistics, so a pass's contents can be judged without a
// screenshot or a graphics debugger.
#include "host.h"

#include <wincodec.h>

#include <cmath>
#include <thread>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace bg3 {

struct Dump {
  uint32_t id;
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
  uint32_t image_id, w, h, texel;
  VkFormat format;
  bool depth;
  uint32_t ordinal;            // pass ordinal inside its command buffer
  uint32_t slot;               // color attachment index, or 99 for depth
  uint64_t pass_fs;
  VkCommandBuffer cmd;
  uint64_t frame = UINT64_MAX; // submit tag, set when the command buffer is submitted
};

static std::mutex g_mu;
static std::vector<Dump> g_dumps;
static uint32_t g_next = 1;
static VkDeviceSize g_bytes = 0;
static bool g_budget_logged = false;

// Bytes per texel of what vkCmdCopyImageToBuffer writes; 0 = not handled.
static uint32_t texel_size(VkFormat f, bool depth) {
  if (depth) {
    switch (f) {
      case VK_FORMAT_D16_UNORM: return 2;
      case VK_FORMAT_X8_D24_UNORM_PACK32: case VK_FORMAT_D24_UNORM_S8_UINT:
      case VK_FORMAT_D32_SFLOAT: case VK_FORMAT_D32_SFLOAT_S8_UINT: return 4;
      default: return 0;
    }
  }
  switch (f) {
    case VK_FORMAT_R8_UNORM: return 1;
    case VK_FORMAT_R8G8_UNORM: case VK_FORMAT_R16_UNORM: case VK_FORMAT_R16_SFLOAT: case VK_FORMAT_R16_UINT: return 2;
    case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_R8G8B8A8_SRGB: case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB: case VK_FORMAT_A2B10G10R10_UNORM_PACK32: case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32: case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32: case VK_FORMAT_R16G16_SFLOAT:
    case VK_FORMAT_R16G16_UNORM: case VK_FORMAT_R16G16_SNORM: case VK_FORMAT_R32_SFLOAT: case VK_FORMAT_R32_UINT:
    case VK_FORMAT_R8G8B8A8_SNORM:
      return 4;
    case VK_FORMAT_R16G16B16A16_SFLOAT: case VK_FORMAT_R16G16B16A16_UNORM: case VK_FORMAT_R32G32_SFLOAT:
    case VK_FORMAT_R32G32_UINT:
      return 8;
    case VK_FORMAT_R32G32B32A32_SFLOAT: case VK_FORMAT_R32G32B32A32_UINT: return 16;
    default: return 0;
  }
}

static bool is_depth_format(VkFormat f) {
  return f == VK_FORMAT_D16_UNORM || f == VK_FORMAT_X8_D24_UNORM_PACK32 || f == VK_FORMAT_D24_UNORM_S8_UINT ||
         f == VK_FORMAT_D32_SFLOAT || f == VK_FORMAT_D32_SFLOAT_S8_UINT || f == VK_FORMAT_D16_UNORM_S8_UINT;
}
static bool has_stencil(VkFormat f) {
  return f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D32_SFLOAT_S8_UINT || f == VK_FORMAT_D16_UNORM_S8_UINT;
}

uint32_t find_memory_type(DeviceData* dd, uint32_t bits, VkMemoryPropertyFlags want) {
  static VkPhysicalDeviceMemoryProperties props{};
  static std::once_flag once;
  std::call_once(once, [&] {
    if (dd->inst && dd->inst->GetPhysicalDeviceMemoryProperties)
      dd->inst->GetPhysicalDeviceMemoryProperties(dd->physical, &props);
  });
  for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
    if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & want) == want) return i;
  return UINT32_MAX;
}

static uint32_t host_memory_type(DeviceData* dd, uint32_t bits) {
  uint32_t t = find_memory_type(dd, bits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                             VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
  return t != UINT32_MAX ? t : find_memory_type(dd, bits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

static bool record_copy(VkCommandBuffer cmd, DeviceData* dd, VkImage image, VkImageLayout layout, const ImageRec& rec,
                        bool depth, Dump& d) {
  VkFormat fmt = rec.ci.format;
  d.texel = texel_size(fmt, depth);
  d.w = rec.ci.extent.width;
  d.h = rec.ci.extent.height;
  d.size = VkDeviceSize(d.w) * d.h * d.texel;

  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = d.size;
  bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  if (dd->vk.CreateBuffer(dd->device, &bci, nullptr, &d.buffer) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  dd->vk.GetBufferMemoryRequirements(dd->device, d.buffer, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = host_memory_type(dd, req.memoryTypeBits);
  if (mai.memoryTypeIndex == UINT32_MAX || dd->vk.AllocateMemory(dd->device, &mai, nullptr, &d.memory) != VK_SUCCESS) {
    dd->vk.DestroyBuffer(dd->device, d.buffer, nullptr);
    d.buffer = VK_NULL_HANDLE;
    return false;
  }
  dd->vk.BindBufferMemory(dd->device, d.buffer, d.memory, 0);

  VkImageAspectFlags barrier_aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT | (has_stencil(fmt) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0)
                                            : VK_IMAGE_ASPECT_COLOR_BIT;
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  b.oldLayout = layout;
  b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange = {barrier_aspect, 0, 1, 0, 1};
  dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                            nullptr, 1, &b);
  VkBufferImageCopy region{};
  region.imageSubresource = {VkImageAspectFlags(depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 0, 1};
  region.imageExtent = {d.w, d.h, 1};
  dd->vk.CmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, d.buffer, 1, &region);
  b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  b.newLayout = layout;
  dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                            nullptr, 1, &b);
  return true;
}

std::string trace_dump_pass(VkCommandBuffer cmd, CmdState* s) {
  if (!trace_recording() || !cfg_int("dump_images", 1)) return {};
  DeviceData* dd = device_of(cmd);
  const VkDeviceSize budget = VkDeviceSize(cfg_int("dump_max_mb", 6144)) << 20;
  const uint32_t min_size = uint32_t(cfg_int("dump_min_size", 64));
  if (s->pass.width < min_size || s->pass.height < min_size) return {};

  std::string ids;
  auto one = [&](VkImage image, VkImageLayout layout, uint32_t slot, bool depth) {
    if (!image || layout == VK_IMAGE_LAYOUT_UNDEFINED) return;
    ImageRec rec;
    {
      std::shared_lock<std::shared_mutex> lock(tracker().mu);
      auto it = tracker().images.find(image);
      if (it == tracker().images.end()) return;
      rec = it->second;
    }
    if (rec.ci.samples != VK_SAMPLE_COUNT_1_BIT || rec.ci.imageType != VK_IMAGE_TYPE_2D) return;
    if (!rec.swapchain && !(rec.ci.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) return;
    if (!texel_size(rec.ci.format, depth) || depth != is_depth_format(rec.ci.format)) return;
    VkDeviceSize bytes = VkDeviceSize(rec.ci.extent.width) * rec.ci.extent.height * texel_size(rec.ci.format, depth);
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_bytes + bytes > budget) {
      if (!g_budget_logged) log("[dump] budget of %d MB reached; later passes are not dumped", cfg_int("dump_max_mb", 6144));
      g_budget_logged = true;
      return;
    }
    Dump d{g_next};
    d.image_id = rec.id;
    d.format = rec.ci.format;
    d.depth = depth;
    d.ordinal = s->pass.ordinal;
    d.slot = slot;
    d.pass_fs = s->pass.first_fs_hash;
    d.cmd = cmd;
    if (!record_copy(cmd, dd, image, layout, rec, depth, d)) return;
    ++g_next;
    g_bytes += d.size;
    g_dumps.push_back(d);
    s->dumps.push_back(d.id);
    ids += (ids.empty() ? "\"d" : ",\"d") + std::to_string(d.id) + "\"";
  };
  for (uint32_t i = 0; i < s->pass.color_count; ++i) one(s->pass.color_images[i], s->color_layouts[i], i, false);
  one(s->pass.depth_image, s->depth_layout, 99, true);
  return ids;
}

void dumps_on_submit(const std::vector<VkCommandBuffer>& cmds, uint64_t frame) {
  std::lock_guard<std::mutex> lock(g_mu);
  for (VkCommandBuffer c : cmds) {
    CmdState* s = cmd_state(c);
    if (!s) continue;
    for (uint32_t id : s->dumps)
      for (auto& d : g_dumps)
        if (d.id == id && d.frame == UINT64_MAX) d.frame = frame;
  }
}

// ---- decoding ----

static float half_to_float(uint16_t h) {
  uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
  float v = e == 0 ? std::ldexp(float(m), -24) : e == 31 ? (m ? NAN : INFINITY) : std::ldexp(float(m | 1024), int(e) - 25);
  return s ? -v : v;
}
static float uf11(uint32_t v, int mbits) {  // unsigned small float, 5-bit exponent
  uint32_t e = v >> mbits, m = v & ((1u << mbits) - 1);
  if (e == 0) return std::ldexp(float(m), -14 - mbits);
  if (e == 31) return m ? NAN : INFINITY;
  return std::ldexp(float(m | (1u << mbits)), int(e) - 15 - mbits);
}

// Decodes one texel into up to 4 floats; returns the channel count.
static int decode(VkFormat f, bool depth, const uint8_t* p, float* o) {
  auto u8 = [&](int i) { return p[i] / 255.0f; };
  auto u16 = [&](int i) { return reinterpret_cast<const uint16_t*>(p)[i]; };
  auto u32 = [&](int i) { return reinterpret_cast<const uint32_t*>(p)[i]; };
  auto f32 = [&](int i) { return reinterpret_cast<const float*>(p)[i]; };
  if (depth) {
    switch (f) {
      case VK_FORMAT_D16_UNORM: o[0] = u16(0) / 65535.0f; return 1;
      case VK_FORMAT_X8_D24_UNORM_PACK32: case VK_FORMAT_D24_UNORM_S8_UINT: o[0] = (u32(0) & 0xFFFFFF) / 16777215.0f; return 1;
      default: o[0] = f32(0); return 1;
    }
  }
  switch (f) {
    case VK_FORMAT_R8_UNORM: o[0] = u8(0); return 1;
    case VK_FORMAT_R8G8_UNORM: o[0] = u8(0); o[1] = u8(1); return 2;
    case VK_FORMAT_R16_UNORM: o[0] = u16(0) / 65535.0f; return 1;
    case VK_FORMAT_R16_UINT: o[0] = float(u16(0)); return 1;
    case VK_FORMAT_R16_SFLOAT: o[0] = half_to_float(u16(0)); return 1;
    case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_R8G8B8A8_SRGB:
      for (int i = 0; i < 4; ++i) o[i] = u8(i);
      return 4;
    case VK_FORMAT_R8G8B8A8_SNORM:
      for (int i = 0; i < 4; ++i) o[i] = std::fmax(int8_t(p[i]) / 127.0f, -1.0f);
      return 4;
    case VK_FORMAT_B8G8R8A8_UNORM: case VK_FORMAT_B8G8R8A8_SRGB:
      o[0] = u8(2); o[1] = u8(1); o[2] = u8(0); o[3] = u8(3);
      return 4;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32: {
      uint32_t v = u32(0);
      o[0] = (v & 1023) / 1023.0f; o[1] = ((v >> 10) & 1023) / 1023.0f; o[2] = ((v >> 20) & 1023) / 1023.0f; o[3] = (v >> 30) / 3.0f;
      return 4;
    }
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32: {
      uint32_t v = u32(0);
      o[2] = (v & 1023) / 1023.0f; o[1] = ((v >> 10) & 1023) / 1023.0f; o[0] = ((v >> 20) & 1023) / 1023.0f; o[3] = (v >> 30) / 3.0f;
      return 4;
    }
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32: {
      uint32_t v = u32(0);
      o[0] = uf11(v & 2047, 6); o[1] = uf11((v >> 11) & 2047, 6); o[2] = uf11(v >> 22, 5);
      return 3;
    }
    case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32: {
      uint32_t v = u32(0);
      float sc = std::ldexp(1.0f, int(v >> 27) - 15 - 9);
      o[0] = (v & 511) * sc; o[1] = ((v >> 9) & 511) * sc; o[2] = ((v >> 18) & 511) * sc;
      return 3;
    }
    case VK_FORMAT_R16G16_SFLOAT: o[0] = half_to_float(u16(0)); o[1] = half_to_float(u16(1)); return 2;
    case VK_FORMAT_R16G16_UNORM: o[0] = u16(0) / 65535.0f; o[1] = u16(1) / 65535.0f; return 2;
    case VK_FORMAT_R16G16_SNORM:
      o[0] = std::fmax(int16_t(u16(0)) / 32767.0f, -1.0f); o[1] = std::fmax(int16_t(u16(1)) / 32767.0f, -1.0f);
      return 2;
    case VK_FORMAT_R32_SFLOAT: o[0] = f32(0); return 1;
    case VK_FORMAT_R32_UINT: o[0] = float(u32(0)); return 1;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
      for (int i = 0; i < 4; ++i) o[i] = half_to_float(u16(i));
      return 4;
    case VK_FORMAT_R16G16B16A16_UNORM:
      for (int i = 0; i < 4; ++i) o[i] = u16(i) / 65535.0f;
      return 4;
    case VK_FORMAT_R32G32_SFLOAT: o[0] = f32(0); o[1] = f32(1); return 2;
    case VK_FORMAT_R32G32_UINT: o[0] = float(u32(0)); o[1] = float(u32(1)); return 2;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
      for (int i = 0; i < 4; ++i) o[i] = f32(i);
      return 4;
    case VK_FORMAT_R32G32B32A32_UINT:
      for (int i = 0; i < 4; ++i) o[i] = float(u32(i));
      return 4;
    default: return 0;
  }
}

static bool is_hdr(VkFormat f) {
  return f == VK_FORMAT_B10G11R11_UFLOAT_PACK32 || f == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32 ||
         f == VK_FORMAT_R16G16B16A16_SFLOAT || f == VK_FORMAT_R32G32B32A32_SFLOAT;
}

bool write_png(const std::wstring& path, uint32_t w, uint32_t h, const std::vector<uint8_t>& rgb) {
  IWICImagingFactory* f = nullptr;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return false;
  IWICStream* st = nullptr;
  IWICBitmapEncoder* enc = nullptr;
  IWICBitmapFrameEncode* fr = nullptr;
  bool ok = SUCCEEDED(f->CreateStream(&st)) && SUCCEEDED(st->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
            SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
            SUCCEEDED(enc->Initialize(st, WICBitmapEncoderNoCache)) && SUCCEEDED(enc->CreateNewFrame(&fr, nullptr)) &&
            SUCCEEDED(fr->Initialize(nullptr)) && SUCCEEDED(fr->SetSize(w, h));
  if (ok) {
    // The PNG encoder takes BGR.
    std::vector<uint8_t> bgr(rgb);
    for (size_t i = 0; i + 2 < bgr.size(); i += 3) std::swap(bgr[i], bgr[i + 2]);
    WICPixelFormatGUID pf = GUID_WICPixelFormat24bppBGR;
    ok = SUCCEEDED(fr->SetPixelFormat(&pf)) && IsEqualGUID(pf, GUID_WICPixelFormat24bppBGR) &&
         SUCCEEDED(fr->WritePixels(h, w * 3, UINT(bgr.size()), bgr.data())) &&
         SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit());
  }
  if (fr) fr->Release();
  if (enc) enc->Release();
  if (st) st->Release();
  f->Release();
  return ok;
}

bool write_png_rgb(const std::string& path, uint32_t w, uint32_t h, const std::vector<uint8_t>& rgb) {
  // COM may not be initialised on Streamline's present thread.
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  bool ok = write_png(std::wstring(path.begin(), path.end()), w, h, rgb);
  if (SUCCEEDED(hr)) CoUninitialize();
  return ok;
}

static std::string format_name(VkFormat f) {
  switch (f) {
    case VK_FORMAT_R8_UNORM: return "R8";
    case VK_FORMAT_R8G8_UNORM: return "RG8";
    case VK_FORMAT_R8G8B8A8_UNORM: return "RGBA8";
    case VK_FORMAT_R8G8B8A8_SRGB: return "RGBA8_SRGB";
    case VK_FORMAT_R8G8B8A8_SNORM: return "RGBA8_SNORM";
    case VK_FORMAT_B8G8R8A8_UNORM: return "BGRA8";
    case VK_FORMAT_B8G8R8A8_SRGB: return "BGRA8_SRGB";
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return "RGB10A2";
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32: return "BGR10A2";
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32: return "R11G11B10F";
    case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32: return "RGB9E5";
    case VK_FORMAT_R16_SFLOAT: return "R16F";
    case VK_FORMAT_R16_UNORM: return "R16";
    case VK_FORMAT_R16_UINT: return "R16U";
    case VK_FORMAT_R16G16_SFLOAT: return "RG16F";
    case VK_FORMAT_R16G16_UNORM: return "RG16";
    case VK_FORMAT_R16G16_SNORM: return "RG16_SNORM";
    case VK_FORMAT_R16G16B16A16_SFLOAT: return "RGBA16F";
    case VK_FORMAT_R16G16B16A16_UNORM: return "RGBA16";
    case VK_FORMAT_R32_SFLOAT: return "R32F";
    case VK_FORMAT_R32_UINT: return "R32U";
    case VK_FORMAT_R32G32_SFLOAT: return "RG32F";
    case VK_FORMAT_R32G32_UINT: return "RG32U";
    case VK_FORMAT_R32G32B32A32_SFLOAT: return "RGBA32F";
    case VK_FORMAT_R32G32B32A32_UINT: return "RGBA32U";
    case VK_FORMAT_D16_UNORM: return "D16";
    case VK_FORMAT_X8_D24_UNORM_PACK32: return "X8D24";
    case VK_FORMAT_D24_UNORM_S8_UINT: return "D24S8";
    case VK_FORMAT_D32_SFLOAT: return "D32F";
    case VK_FORMAT_D32_SFLOAT_S8_UINT: return "D32FS8";
    default: return "fmt" + std::to_string(int(f));
  }
}

// Decodes one dump: statistics over every texel, plus a downscaled preview.
// Preview mapping: HDR colour → Reinhard + gamma; two-channel / signed data →
// 0.5 + 0.5 * v / max|v|; one channel → min..max stretch; UNORM colour as is.
// A fourth channel that varies gets its own *_a.png.
static bool g_dump_raw = false;   // set per capture by dumps_finish
static std::string process(const Dump& d, const uint8_t* data, const std::string& dir, int max_w) {
  const size_t n = size_t(d.w) * d.h;
  float mn[4] = {INFINITY, INFINITY, INFINITY, INFINITY}, mx[4] = {-INFINITY, -INFINITY, -INFINITY, -INFINITY};
  double sum[4] = {};
  int ch = 0;
  size_t nonfinite = 0;
  for (size_t i = 0; i < n; ++i) {
    float v[4] = {};
    ch = decode(d.format, d.depth, data + i * d.texel, v);
    for (int c = 0; c < ch; ++c) {
      if (!std::isfinite(v[c])) { ++nonfinite; continue; }
      mn[c] = std::fmin(mn[c], v[c]);
      mx[c] = std::fmax(mx[c], v[c]);
      sum[c] += v[c];
    }
  }
  if (!ch) return {};

  uint32_t ow = d.w, oh = d.h;
  if (max_w > 0 && ow > uint32_t(max_w)) {
    oh = uint32_t(uint64_t(oh) * max_w / ow);
    ow = uint32_t(max_w);
  }
  const bool hdr = is_hdr(d.format);
  const bool unorm_color = !hdr && ch >= 3 && !d.depth;
  float absmax[4];
  for (int c = 0; c < 4; ++c) absmax[c] = std::fmax(std::fabs(mn[c]), std::fabs(mx[c])) + 1e-20f;
  std::vector<uint8_t> rgb(size_t(ow) * oh * 3), alpha(size_t(ow) * oh);
  for (uint32_t y = 0; y < oh; ++y)
    for (uint32_t x = 0; x < ow; ++x) {
      uint32_t sx = uint32_t(uint64_t(x) * d.w / ow), sy = uint32_t(uint64_t(y) * d.h / oh);
      float v[4] = {};
      decode(d.format, d.depth, data + (size_t(sy) * d.w + sx) * d.texel, v);
      float out[3];
      for (int c = 0; c < 3; ++c) {
        int src = ch == 1 ? 0 : c;
        float t;
        if (ch == 2 && c == 2) t = 0.0f;
        else if (hdr) t = std::pow(std::fmax(v[src], 0.0f) / (1.0f + std::fmax(v[src], 0.0f)), 1.0f / 2.2f);
        else if (unorm_color) t = v[src];
        else if (ch == 1) t = mx[0] > mn[0] ? (v[0] - mn[0]) / (mx[0] - mn[0]) : 0.0f;
        else t = 0.5f + 0.5f * v[src] / absmax[src];
        out[c] = std::isfinite(t) ? std::fmin(std::fmax(t, 0.0f), 1.0f) : 1.0f;
      }
      size_t o = (size_t(y) * ow + x);
      for (int c = 0; c < 3; ++c) rgb[o * 3 + c] = uint8_t(out[c] * 255.0f + 0.5f);
      alpha[o] = uint8_t(std::fmin(std::fmax(v[3], 0.0f), 1.0f) * 255.0f + 0.5f);
    }

  char base[160];
  snprintf(base, sizeof base, "d%u_p%u_%s%u_i%u_%s", d.id, d.ordinal, d.depth ? "depth" : "c", d.depth ? 0 : d.slot,
           d.image_id, format_name(d.format).c_str());
  std::string png = std::string(base) + ".png";
  std::wstring wdir(dir.begin(), dir.end());
  write_png(wdir + L"\\" + std::wstring(png.begin(), png.end()), ow, oh, rgb);
  // dump_raw=1: full-resolution float32 RGBA (no header; w, h in dumps.json) for
  // the images A/B comparisons need: our own fake passes (RR guides, rr_post
  // images, the DLSS output), the game's grade output (its old DLSS input,
  // 0592680b) and the tonemapped frame (d25b8086).
  if (g_dump_raw && !d.depth &&
      (d.pass_fs == 0 || d.pass_fs == 0x0592680bc336dca3ull || d.pass_fs == 0xd25b8086de1734d5ull)) {
    std::vector<float> raw(n * 4);
    for (size_t i = 0; i < n; ++i) decode(d.format, d.depth, data + i * d.texel, &raw[i * 4]);
    std::string f32 = std::string(base) + ".f32";
    write_file(dir + "\\" + f32, raw.data(), raw.size() * sizeof(float));
  }
  bool alpha_varies = ch == 4 && mx[3] > mn[3];
  if (alpha_varies) {
    std::vector<uint8_t> a3(alpha.size() * 3);
    for (size_t i = 0; i < alpha.size(); ++i) a3[i * 3] = a3[i * 3 + 1] = a3[i * 3 + 2] = alpha[i];
    std::string apng = std::string(base) + "_a.png";
    write_png(wdir + L"\\" + std::wstring(apng.begin(), apng.end()), ow, oh, a3);
  }

  std::string j = "\"d" + std::to_string(d.id) + "\":{\"png\":\"img/" + png + "\",\"image\":\"i" +
                  std::to_string(d.image_id) + "\",\"pass_ordinal\":" + std::to_string(d.ordinal) +
                  ",\"slot\":" + (d.depth ? std::string("\"depth\"") : std::to_string(d.slot)) + ",\"fmt\":\"" +
                  format_name(d.format) + "\",\"w\":" + std::to_string(d.w) + ",\"h\":" + std::to_string(d.h);
  char b[96];
  snprintf(b, sizeof b, ",\"pass_fs\":\"%016llx\",\"nonfinite\":%zu", (unsigned long long)d.pass_fs, nonfinite);
  j += b;
  auto arr = [&](const char* name, auto get) {
    j += std::string(",\"") + name + "\":[";
    for (int c = 0; c < ch; ++c) {
      double v = double(get(c));
      if (std::isfinite(v)) snprintf(b, sizeof b, "%s%.6g", c ? "," : "", v);
      else snprintf(b, sizeof b, "%snull", c ? "," : "");  // JSON has no inf/nan
      j += b;
    }
    j += "]";
  };
  arr("min", [&](int c) { return mn[c]; });
  arr("max", [&](int c) { return mx[c]; });
  arr("mean", [&](int c) { return sum[c] / double(n); });
  return j + "}";
}

void dumps_finish(uint64_t frame, const std::string& capture_dir, VkQueue queue) {
  DeviceData* dd = main_device();
  std::vector<Dump> mine, all;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    all.swap(g_dumps);
    g_bytes = 0;
    g_budget_logged = false;
  }
  if (all.empty()) return;
  for (auto& d : all)
    if (d.frame == frame) mine.push_back(d);

  dd->vk.QueueWaitIdle(queue);
  if (dd->graphics_queue && dd->graphics_queue != queue) dd->vk.QueueWaitIdle(dd->graphics_queue);

  std::string dir = capture_dir + "\\img";
  make_dirs(dir);
  int max_w = cfg_int("dump_png_width", 1280);
  g_dump_raw = cfg_int("dump_raw", 0) != 0;
  ULONGLONG t0 = GetTickCount64();
  std::vector<std::string> parts(mine.size());
  std::vector<void*> maps(mine.size(), nullptr);
  for (size_t i = 0; i < mine.size(); ++i) dd->vk.MapMemory(dd->device, mine[i].memory, 0, VK_WHOLE_SIZE, 0, &maps[i]);
  // Own threads: COM is initialised per thread and the game's thread is off-limits.
  unsigned nthreads = (std::max)(1u, (std::min)(8u, std::thread::hardware_concurrency()));
  std::atomic<size_t> next{0};
  std::vector<std::thread> pool;
  for (unsigned t = 0; t < nthreads; ++t)
    pool.emplace_back([&] {
      HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
      for (size_t i; (i = next++) < mine.size();)
        if (maps[i]) parts[i] = process(mine[i], static_cast<const uint8_t*>(maps[i]), dir, max_w);
      if (SUCCEEDED(co)) CoUninitialize();
    });
  for (auto& t : pool) t.join();
  for (size_t i = 0; i < mine.size(); ++i)
    if (maps[i]) dd->vk.UnmapMemory(dd->device, mine[i].memory);

  std::string j = "{\n";
  bool first = true;
  for (auto& p : parts)
    if (!p.empty()) {
      j += (first ? "" : ",\n") + p;
      first = false;
    }
  j += "\n}\n";
  write_file(capture_dir + "\\dumps.json", j.data(), j.size());
  log("[dump] %zu of %zu dumps belong to frame %llu; decoded in %llu ms", mine.size(), all.size(),
      (unsigned long long)frame, (unsigned long long)(GetTickCount64() - t0));

  // Dumps recorded into command buffers that are not submitted yet must stay
  // alive until those run, so everything goes through deferred destruction.
  for (auto& d : all) {
    defer_destroy(BG3_DEFER_BUFFER, uint64_t(d.buffer));
    defer_destroy(BG3_DEFER_MEMORY, uint64_t(d.memory));
  }
}

}  // namespace bg3
