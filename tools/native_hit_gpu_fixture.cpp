// Windowless analytic GPU fixture of the hidden-hit shading functions (CTest native_hit_gpu_fixture).
// Runs the REAL native_hit_direct (point/spot + box lights, the screen's light policy), native_hit_sun
// (disc sampling), native_hit_ao and hd_scatter through tools/native_hit_gpu_fixture.hlsl on closed-form
// synthetic scenes built from opaque triangles in a real TLAS, and compares every result with an
// independent double-precision reference written here from the declared formulas:
//   - incident light: bg3_light_eval's convention (shaders/rt/bg3_lights.hlsli, f469db78): point
//     col x att / (4 pi), spot col x att x t^2 / pi, att = max(|sat(1 - (d2/r2)^2)|, 1e-4)^m4 x
//     (1 / max(d2, 1e-4) + |m5|) or (4 / r2 + |m5|) for m5 < 0; x b13[0] when m3 < 0;
//   - box light: f469db78's box loop (shaders/rt/bg3_boxes.hlsli): colour x ramp x edges along _m9;
//   - BSDF: the declared PBRT/Ashikhmin-Shirley FresnelBlend with GGX D (common/transport_bsdf.h);
//   - AO: the hybrid GI rule (gi_trace_rt.hlsl): weight 1 - (t/R)^2, closed form for a wall;
//   - sun disc: rt_shadow.hlsl's uniform tangent-disc sample, circular-segment area for an edge;
//   - emission at hits (rt_hit_emission): the D-EM record radiance (em_common.hlsli em_radiance), the
//     record policy and the latency shift, on coplanar emissive quads in an emissive TLAS of their own.
// Tolerance (derived, not tuned): every consumer stores these radiances in FP16 targets (giRaw,
// rf output: rgba16f), whose rounding is half an ULP = 2^-11 relative. A GPU FP32 difference below
// that is invisible; the FP32 shader error itself (pow/exp2/log2 within 3 + 2|x| ULP, rsqrt 2 ULP,
// Vulkan SPIR-V precision table) is ~1e-6 relative, far below it. So |gpu - ref| <= 2^-11 |ref| +
// 2^-24 (the FP16 smallest subnormal). An exact zero (occluded, out of range, channel) must be 0.
// Monte Carlo cases (disc, AO) use 5 standard deviations of the estimator, from its analytic variance.
// Exit 77 (CTest SKIP_RETURN_CODE): no Vulkan device with ray queries, or no Khronos validation layer.
#undef VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <windows.h>
#include "../common/native_vt_fields.h"   // nv_two_sided_sign (case 9 reference)
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <vector>
#pragma comment(lib, "vulkan-1.lib")
#include "../common/ls_exact_k.h"        // case 8: the D-LS exact-k selection (ls_k), as C++ for the reference
#include "../common/emissive_policy.h"   // em::EmRec and its flags: the record layout the host writes

namespace {
VkDevice dev;
VkPhysicalDeviceMemoryProperties memory;
std::atomic<unsigned> validationErrors{};
void check(bool yes, const char* what) {
  if (!yes) { std::fprintf(stderr, "FAIL %s\n", what); std::exit(1); }
}
void vkcheck(VkResult r, const char* what) {
  if (r != VK_SUCCESS) { std::fprintf(stderr, "FAIL %s result=%d\n", what, r); std::exit(1); }
}
VKAPI_ATTR VkBool32 VKAPI_CALL validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                          const VkDebugUtilsMessengerCallbackDataEXT* info, void*) {
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    if (validationErrors.fetch_add(1) < 16) std::fprintf(stderr, "VALIDATION %s\n", info->pMessage);
  return VK_FALSE;
}
uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags) {
  for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
    if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
  check(false, "memory type");
  return 0;
}
struct Buffer {
  VkBuffer b{}; VkDeviceMemory m{}; VkDeviceAddress address{}; void* map{}; VkDeviceSize size{};
  ~Buffer() { if (map) vkUnmapMemory(dev, m); if (b) vkDestroyBuffer(dev, b, nullptr); if (m) vkFreeMemory(dev, m, nullptr); }
};
void make_buffer(Buffer& out, VkDeviceSize bytes, VkBufferUsageFlags usage, bool host = true) {
  out.size = bytes;
  VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  ci.size = bytes; ci.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  vkcheck(vkCreateBuffer(dev, &ci, nullptr, &out.b), "buffer");
  VkMemoryRequirements req; vkGetBufferMemoryRequirements(dev, out.b, &req);
  VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO}; flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &flags};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = memory_type(req.memoryTypeBits, host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                                             : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  vkcheck(vkAllocateMemory(dev, &ai, nullptr, &out.m), "buffer memory");
  vkcheck(vkBindBufferMemory(dev, out.b, out.m, 0), "bind buffer");
  VkBufferDeviceAddressInfo address{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; address.buffer = out.b;
  out.address = vkGetBufferDeviceAddress(dev, &address);
  if (host) { vkcheck(vkMapMemory(dev, out.m, 0, bytes, 0, &out.map), "map"); std::memset(out.map, 0, size_t(bytes)); }
}
std::vector<uint32_t> read_shader(const char* path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  check(bool(f), path);
  auto bytes = f.tellg();
  check(bytes > 0 && uint64_t(bytes) % 4 == 0, "shader size");
  std::vector<uint32_t> code(size_t(bytes) / 4);
  f.seekg(0); f.read(reinterpret_cast<char*>(code.data()), bytes);
  check(bool(f), "shader read");
  return code;
}
void barrier(VkCommandBuffer c, VkPipelineStageFlags src, VkAccessFlags access, VkPipelineStageFlags dst, VkAccessFlags read) {
  VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; b.srcAccessMask = access; b.dstAccessMask = read;
  vkCmdPipelineBarrier(c, src, dst, 0, 1, &b, 0, nullptr, 0, nullptr);
}

// gi_common.hlsli Push (core/gi.cpp's mirror; 256 bytes, the offsets spirv-reflect reports).
struct Push {
  uint32_t full[2], half[2];
  uint32_t frame;
  float strength, radius, thickness;
  uint32_t rays, steps, debug;
  float history, ao;
  float cs_length, cs_strength;
  uint32_t cs_steps;
  float mv_scale[2];
  uint32_t has_mv, bounce_spec;
  float rt_bias, rt_tmax;
  uint32_t rt_debug;
  float rt_strength, rt_ao_radius;
  uint32_t rt_ao_rays, ls_kmode, sharc_fx;
  float gi_rt_radius, gi_rt_tol;
  uint32_t gi_rt_fallback, rt_stats;
  float sun_radius;
  uint32_t res_shift, qa_zero, rt_max_cand;
  float ls_source_radius, ls_clear, ls_min_radius;
  uint32_t ls_samples, ls_shadowed, ls_debug, ls_stats, ls_pass, ls_soft;
  float ls_strength;
  uint32_t ls_has_mask, ls_flags;
  float rf_max_rough, rf_mirror_rough, rf_tmax, rf_depth_tol, rf_edge, rf_strength;
  uint32_t rf_debug, rf_sample, rf_so, rf_validate, rf_models, rf_stats;
  uint32_t rt_backlit, mat_classes, ao_direct, rt_origin;
};
static_assert(sizeof(Push) == 256 && offsetof(Push, rt_tmax) == 84 && offsetof(Push, rt_ao_rays) == 100 &&
              offsetof(Push, gi_rt_radius) == 112 && offsetof(Push, sun_radius) == 128 && offsetof(Push, ls_source_radius) == 144 &&
              offsetof(Push, ls_flags) == 188, "Push must match gi_common.hlsli");

// ---- double-precision reference ----
struct D3 { double x, y, z; };
D3 operator+(D3 a, D3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
D3 operator-(D3 a, D3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
D3 operator*(D3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
D3 mul(D3 a, D3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
double dot(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double len(D3 a) { return std::sqrt(dot(a, a)); }
D3 unit(D3 a) { return a * (1.0 / len(a)); }
D3 f3(const float* v) { return {v[0], v[1], v[2]}; }
constexpr double kPi = 3.14159265358979323846;

struct Mat { D3 albedo; double rough, metal, refl; };
// Declared FresnelBlend (PBRT 9.5 / Ashikhmin-Shirley 2000) with isotropic GGX D, on the parameters
// of nt_class0_parameters: Rd = albedo (1 - m), Rs = 0.16 refl^2 (1 - m) + albedo m,
// alpha = max(0.09, roughness)^2, GGX a2 = alpha^2.
D3 fresnel_blend(const Mat& m, D3 n, D3 wo, D3 wi) {
  const double co = dot(n, wo), ci = dot(n, wi);
  if (co <= 0 || ci <= 0) return {0, 0, 0};
  const D3 Rd = m.albedo * (1 - m.metal);
  const double f0 = 0.16 * m.refl * m.refl * (1 - m.metal);
  const D3 Rs = D3{f0, f0, f0} + m.albedo * m.metal;
  const double r = std::max(0.09, m.rough), alpha = r * r, a2 = alpha * alpha;
  const D3 sum = wo + wi;
  const D3 h = unit(sum);
  const double c = std::min(1.0, 0.5 * len(sum)), nh = dot(n, h);
  const double dd = 1 + (a2 - 1) * nh * nh, D = a2 / (kPi * dd * dd);
  const double spec = D / (4 * c * std::max(co, ci)), t5 = std::pow(1 - c, 5);
  auto gate = [](double x) { return 1 - std::pow(1 - x / 2, 5); };
  const double diff = 28.0 / (23.0 * kPi) * gate(co) * gate(ci);
  auto ch = [&](double rd, double rs) { return diff * rd * (1 - rs) + spec * (rs + (1 - rs) * t5); };
  return {ch(Rd.x, Rs.x), ch(Rd.y, Rs.y), ch(Rd.z, Rs.z)};
}
struct Light {
  float pos[3], inv_r, col[3], cos_outer, dir[3], cos_inner, m3, m4, m5; uint32_t chan, flags; float strength; uint32_t m9[2];
};
static_assert(sizeof(Light) == 80);
// bg3_light_eval's incident amplitude before N.L (the k of f469db78 without its nol factor).
D3 light_incident(const Light& l, bool spot, D3 P, uint32_t channel, double global, D3& L) {
  L = {0, 0, 1};
  if ((l.chan & (1u << channel)) == 0) return {0, 0, 0};
  const D3 delta = f3(l.pos) - P;
  const double d2 = dot(delta, delta), ir2 = double(l.inv_r) * l.inv_r;
  L = delta * (1 / std::sqrt(d2));
  if (!(d2 < 1 / ir2)) return {0, 0, 0};
  const double x = d2 * ir2;
  double att = std::pow(std::max(std::abs(std::clamp(1 - x * x, 0.0, 1.0)), 9.9999997e-05), l.m4);
  const double bias = std::abs(l.m5);
  att *= l.m5 < 0 ? 4 * ir2 + bias : 1 / std::max(d2, 9.9999997e-05) + bias;
  double k = att / (4 * kPi);
  if (spot) {
    const double c = -dot(L, unit(f3(l.dir)));
    if (!(c > l.cos_outer)) return {0, 0, 0};
    const double t = std::clamp((c - l.cos_outer) / std::max(double(l.cos_inner) - l.cos_outer, 9.9999997e-05), 0.0, 1.0);
    k = att * t * t / kPi;
  }
  return f3(l.col) * (k * (l.m3 < 0 ? global : 1.0));
}
struct Box {
  float col[3]; uint32_t m1; float ramp[4], edge[2]; uint32_t m4; float strength; float r0[4], r1[4], r2[4], m7[3]; uint32_t flags;
  float dir[3], m10;
};
static_assert(sizeof(Box) == 128);
double box_curve(uint32_t k, double t) {
  const double t2 = t * t, s = std::clamp(t, 0.0, 1.0);
  return k == 0 ? t : k == 1 ? t2 : k == 2 ? s * s * (3 - 2 * s) : k == 3 ? t2 * t * (t * (t * 6 - 15) + 10) : 0.0;
}
// f469db78's box term along _m9 before N.L: colour x falloff (x b13[0] with _m1 bit 0); 0 outside.
D3 box_incident(const Box& b, D3 P, uint32_t channel, double global) {
  if (((b.m4 & 0xffffffu) & (1u << channel)) == 0) return {0, 0, 0};
  auto row = [&](const float* r) { return r[0] * P.x + r[1] * P.y + r[2] * P.z + r[3]; };
  const double u[3] = {row(b.r0), row(b.r1), row(b.r2)};
  for (double c : u) if (!(std::abs(c - 0.5) < 0.5)) return {0, 0, 0};
  const uint32_t k = b.m4 >> 24;
  double z = 1;
  if (u[2] < b.ramp[0]) z = box_curve(k, u[2] * b.ramp[2]);
  else if (u[2] > b.ramp[1]) z = box_curve(k, (1 - u[2]) * b.ramp[3]);
  const double ex = (0.5 - std::abs(u[0] - 0.5)) * b.edge[0], ey = (0.5 - std::abs(u[1] - 0.5)) * b.edge[1];
  const double g = (ex < 1 ? box_curve(k, ex) : 1) * (ey < 1 ? box_curve(k, ey) : 1) * z;
  return f3(b.col) * (g * ((b.m1 & 1u) ? global : 1.0));
}

// ---- scene ----
struct Vec { float x, y, z; };
std::vector<float> soup;   // xyz per vertex, 3 vertices per triangle, camera-relative lighting space
void tri(Vec a, Vec b, Vec c) { for (Vec v : {a, b, c}) { soup.push_back(v.x); soup.push_back(v.y); soup.push_back(v.z); } }
void quad(Vec a, Vec b, Vec c, Vec d) { tri(a, b, c); tri(a, c, d); }
void hquad(float x0, float x1, float y0, float y1, float z) { quad({x0, y0, z}, {x1, y0, z}, {x1, y1, z}, {x0, y1, z}); }
void cube(Vec c, float h) {
  const float x0 = c.x - h, x1 = c.x + h, y0 = c.y - h, y1 = c.y + h, z0 = c.z - h, z1 = c.z + h;
  hquad(x0, x1, y0, y1, z0); hquad(x0, x1, y0, y1, z1);
  quad({x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}); quad({x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1});
  quad({x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}); quad({x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1});
}
// Region k: a 30 m x 30 m floor (z = 0, normal +z) centred at (cx(k), 60), 100 m apart (no ray of one
// case reaches another region's geometry: light rays < 10 m, AO rays 1 m, sun rays leave above z 100).
constexpr float kY = 60;
float cx(int k) { return -950.0f + 100.0f * k; }
enum { RPoint, RLamp, ROcc, RSunOpen, RRoof, RDisc, RCorner, RAoOpen, RBox, RBoxShadow, REnergy, REmit, RCount };

struct FxCase { float p[3]; uint32_t mode; float n[3]; uint32_t key; float v[3]; float footprint; float c48[3]; float rough, metal, refl;
                uint32_t channel, pad; };
static_assert(sizeof(FxCase) == 80);
struct FxOut { float a[3]; uint32_t sa; float b[3]; uint32_t sb; float c[3]; uint32_t lights; float ao, factor, unused; uint32_t mstatus; };
static_assert(sizeof(FxOut) == 64);
enum { kFxDirect = 0, kFxSun = 1, kFxAo = 2, kFxFurnace = 3, kFxCombined = 4, kFxSunDir = 5, kFxEmission = 6, kFxExactK = 7, kFxTwoSided = 8 };
// native_hit_emission.hlsli statuses.
enum { kHeUsed = 0, kHeNone = 1, kHeExcluded = 2, kHeUnvalidated = 3 };
constexpr uint32_t kHdOk = 0;

struct Dispatch {
  std::vector<FxCase> cases;
  std::vector<Light> lights; uint32_t points = 0, spots = 0;
  std::vector<Box> boxes;
  float sun_dir[3] = {0, 0, 1}, sun_col[3] = {0, 0, 0}, global = 0.7f;
  Push push{};
};
Push default_push() {
  Push p{};
  // Production defaults (bg3rtx.ini): rt_tmax 300, ls_source_radius 0.05, ls_clear 0.2,
  // ls_min_radius 0, hit_ls_policy 1 (ls_flags bit 24), ao_direct 1, gi_rt_radius 1.
  p.rt_tmax = 300; p.ls_source_radius = 0.05f; p.ls_clear = 0.2f; p.ls_min_radius = 0; p.ls_flags = 1u << 24;
  p.ao_direct = 1; p.gi_rt_radius = 1; p.rt_ao_rays = 8; p.sun_radius = 0.00459f;
  return p;
}
const Mat kMat{{0.8, 0.6, 0.4}, 0.5, 0.0, 0.5};
const D3 kN{0, 0, 1};
const D3 kV = unit(D3{0.3, -0.2, 0.9});
FxCase make_case(Vec p, uint32_t mode, const Mat& m = kMat, uint32_t channel = 0, uint32_t key = 0) {
  FxCase c{};
  c.p[0] = p.x; c.p[1] = p.y; c.p[2] = p.z; c.mode = mode; c.n[2] = 1; c.key = key;
  c.v[0] = float(kV.x); c.v[1] = float(kV.y); c.v[2] = float(kV.z);
  c.c48[0] = float(m.albedo.x); c.c48[1] = float(m.albedo.y); c.c48[2] = float(m.albedo.z);
  c.rough = float(m.rough); c.metal = float(m.metal); c.refl = float(m.refl); c.channel = channel;
  return c;
}
// The reference uses the float inputs the GPU sees.
Mat case_mat(const FxCase& c) { return {f3(c.c48), c.rough, c.metal, c.refl}; }

struct Report {
  int failures = 0, groups = 0;
  void line(const char* name, bool ok, const std::string& detail) {
    ++groups; if (!ok) ++failures;
    std::printf("%-44s %s  %s\n", name, ok ? "PASS" : "FAIL", detail.c_str());
  }
};
double tol(double ref) { return std::ldexp(std::abs(ref), -11) + std::ldexp(1.0, -24); }
std::string fmt(const char* f, double a, double b = 0, double c = 0) { char s[256]; std::snprintf(s, sizeof s, f, a, b, c); return s; }
}  // namespace

int main(int argc, char** argv) {
  check(argc == 2, "usage: native_hit_gpu_fixture native_hit_gpu_fixture.spv");
  const auto start = std::chrono::steady_clock::now();
  SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
  // Private loader: never the game's implicit layers (VkLayer_bg3rtx), only explicit validation.
  SetEnvironmentVariableA("VK_LOADER_LAYERS_DISABLE", "~implicit~");
  SetEnvironmentVariableA("VK_INSTANCE_LAYERS", nullptr);
  SetEnvironmentVariableA("VK_LOADER_LAYERS_ENABLE", nullptr);
  SetEnvironmentVariableA("VK_ADD_LAYER_PATH", nullptr);
  uint32_t nl = 0;
  if (vkEnumerateInstanceLayerProperties(&nl, nullptr) != VK_SUCCESS) { std::puts("SKIP no Vulkan loader"); return 77; }
  std::vector<VkLayerProperties> layers(nl);
  vkcheck(vkEnumerateInstanceLayerProperties(&nl, layers.data()), "layers");
  bool haveValidation = false;
  for (auto& x : layers) haveValidation |= !std::strcmp(x.layerName, "VK_LAYER_KHRONOS_validation");
  if (!haveValidation) { std::puts("SKIP Khronos validation layer not installed (never a silent pass)"); return 77; }
  const char* layer = "VK_LAYER_KHRONOS_validation";
  const char* debugExt = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
  VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
  debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
  debug.pfnUserCallback = validation;
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "native hit analytic fixture"; app.apiVersion = VK_API_VERSION_1_3;
  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, &debug};
  ici.pApplicationInfo = &app; ici.enabledLayerCount = 1; ici.ppEnabledLayerNames = &layer;
  ici.enabledExtensionCount = 1; ici.ppEnabledExtensionNames = &debugExt;
  VkInstance instance;
  if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS) { std::puts("SKIP no Vulkan 1.3 instance"); return 77; }
  auto createDebug = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
  auto destroyDebug = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
  VkDebugUtilsMessengerEXT messenger;
  vkcheck(createDebug(instance, &debug, nullptr, &messenger), "debug messenger");
  auto skip = [&](const char* why) { std::printf("SKIP %s\n", why); destroyDebug(instance, messenger, nullptr); vkDestroyInstance(instance, nullptr); return 77; };
  uint32_t np = 0;
  vkEnumeratePhysicalDevices(instance, &np, nullptr);
  std::vector<VkPhysicalDevice> physical(np);
  if (np) vkEnumeratePhysicalDevices(instance, &np, physical.data());
  VkPhysicalDevice gpu = VK_NULL_HANDLE;
  VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  VkPhysicalDeviceRayQueryFeaturesKHR rq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, &f12};
  VkPhysicalDeviceAccelerationStructureFeaturesKHR as{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &rq};
  VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &as};
  for (auto p : physical) {
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(p, &props);
    if (props.apiVersion < VK_API_VERSION_1_3) continue;
    vkGetPhysicalDeviceFeatures2(p, &features);
    if (as.accelerationStructure && rq.rayQuery && f12.bufferDeviceAddress && f12.runtimeDescriptorArray &&
        f12.shaderSampledImageArrayNonUniformIndexing && features.features.shaderStorageImageExtendedFormats) { gpu = p; break; }
  }
  if (!gpu) return skip("no Vulkan 1.3 device with ray queries (not passed)");
  VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(gpu, &props);
  vkGetPhysicalDeviceMemoryProperties(gpu, &memory);
  VkPhysicalDeviceVulkan11Properties p11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES};
  VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR, &p11};
  VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &asProps};
  vkGetPhysicalDeviceProperties2(gpu, &properties);
  const uint32_t needOps = VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_BALLOT_BIT | VK_SUBGROUP_FEATURE_ARITHMETIC_BIT;
  if ((p11.subgroupSupportedOperations & needOps) != needOps || !(p11.subgroupSupportedStages & VK_SHADER_STAGE_COMPUTE_BIT))
    return skip("no compute subgroup ballot/arithmetic");
  std::printf("GPU=%s driver=%u implicit_layers=disabled validation=1 window=none\n", props.deviceName, props.driverVersion);
  uint32_t nq = 0; vkGetPhysicalDeviceQueueFamilyProperties(gpu, &nq, nullptr);
  std::vector<VkQueueFamilyProperties> families(nq); vkGetPhysicalDeviceQueueFamilyProperties(gpu, &nq, families.data());
  uint32_t family = UINT32_MAX;
  for (uint32_t i = 0; i < nq; ++i) if (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { family = i; break; }
  check(family != UINT32_MAX, "compute queue");
  const char* extensions[] = {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME,
                              VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME};
  VkPhysicalDeviceVulkan12Features use12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  use12.bufferDeviceAddress = VK_TRUE; use12.runtimeDescriptorArray = VK_TRUE; use12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
  VkPhysicalDeviceRayQueryFeaturesKHR useRQ{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, &use12}; useRQ.rayQuery = VK_TRUE;
  VkPhysicalDeviceAccelerationStructureFeaturesKHR useAS{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &useRQ};
  useAS.accelerationStructure = VK_TRUE;
  VkPhysicalDeviceFeatures useCore{}; useCore.shaderStorageImageExtendedFormats = VK_TRUE;
  useCore.shaderSampledImageArrayDynamicIndexing = features.features.shaderSampledImageArrayDynamicIndexing;
  float priority = 0.0f;
  VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qci.queueFamilyIndex = family; qci.queueCount = 1; qci.pQueuePriorities = &priority;
  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &useAS};
  dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci; dci.enabledExtensionCount = 3; dci.ppEnabledExtensionNames = extensions; dci.pEnabledFeatures = &useCore;
  vkcheck(vkCreateDevice(gpu, &dci, nullptr, &dev), "device");
  VkQueue queue; vkGetDeviceQueue(dev, family, 0, &queue);
#define PROC(type, name) auto name = reinterpret_cast<PFN_##type>(vkGetDeviceProcAddr(dev, #type)); check(name != nullptr, #type)
  PROC(vkCreateAccelerationStructureKHR, createAS); PROC(vkDestroyAccelerationStructureKHR, destroyAS);
  PROC(vkGetAccelerationStructureBuildSizesKHR, sizesAS); PROC(vkCmdBuildAccelerationStructuresKHR, buildAS);
  PROC(vkGetAccelerationStructureDeviceAddressKHR, addressAS);
#undef PROC
  Report report;
  {
    // ---- geometry: one opaque BLAS of every region's triangles, one identity instance ----
    for (int k = 0; k < RCount; ++k) hquad(cx(k) - 15, cx(k) + 15, kY - 15, kY + 15, 0);
    cube({cx(RLamp), kY, 2}, 0.1f);                                        // closed lamp around its light
    { const float c = cx(ROcc);
      hquad(c + 1.5f, c + 2.5f, kY - 0.5f, kY + 0.5f, 1.5f);               // A: between the light and P1
      hquad(c - 5, c + 5, kY - 5, kY + 5, 4.0f);                           // B: a ceiling behind the light
      hquad(c - 0.15f, c - 0.05f, kY - 0.05f, kY + 0.05f, 2.95f); }        // C: within ls_clear of the light
    hquad(cx(RRoof) - 15, cx(RRoof) + 15, kY - 15, kY + 15, 5.0f);         // roof slab
    hquad(cx(RDisc), cx(RDisc) + 30, kY - 30, kY + 30, 20.0f);             // half-plane at z 20, edge x = cx
    quad({cx(RCorner), kY - 15, 0}, {cx(RCorner), kY + 15, 0}, {cx(RCorner), kY + 15, 10}, {cx(RCorner), kY - 15, 10});   // wall x = cx
    for (int k : {RBox, RBoxShadow}) hquad(cx(k) - 1.75f, cx(k) - 1.25f, kY - 0.25f, kY + 0.25f, 2.0f);   // D: inside the box
    hquad(cx(RBoxShadow) + 3.75f, cx(RBoxShadow) + 4.25f, kY - 0.25f, kY + 0.25f, 6.0f);                    // E: beyond the box
    hquad(cx(REmit) + 0.5f, cx(REmit) + 1.5f, kY - 0.5f, kY + 0.5f, 2.5f);                                  // F: over the emitter
    const uint32_t triangles = uint32_t(soup.size() / 9);
    Buffer vertices, instances, blasStore, tlasStore, scratch;
    const auto inputUsage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    make_buffer(vertices, soup.size() * 4, inputUsage); std::memcpy(vertices.map, soup.data(), soup.size() * 4);
    make_buffer(instances, 64, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR; geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.triangles = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR};
    geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT; geometry.geometry.triangles.vertexData.deviceAddress = vertices.address;
    geometry.geometry.triangles.vertexStride = 12; geometry.geometry.triangles.maxVertex = triangles * 3 - 1; geometry.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
    VkAccelerationStructureBuildGeometryInfoKHR bottom{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    bottom.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR; bottom.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    bottom.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR; bottom.geometryCount = 1; bottom.pGeometries = &geometry;
    VkAccelerationStructureBuildSizesInfoKHR bs{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    sizesAS(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bottom, &triangles, &bs);
    make_buffer(blasStore, bs.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false);
    VkAccelerationStructureCreateInfoKHR ac{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    ac.buffer = blasStore.b; ac.size = bs.accelerationStructureSize; ac.type = bottom.type;
    VkAccelerationStructureKHR blas; vkcheck(createAS(dev, &ac, nullptr, &blas), "BLAS"); bottom.dstAccelerationStructure = blas;
    VkAccelerationStructureDeviceAddressInfoKHR ba{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR}; ba.accelerationStructure = blas;
    VkAccelerationStructureInstanceKHR object{};
    object.transform.matrix[0][0] = object.transform.matrix[1][1] = object.transform.matrix[2][2] = 1; object.mask = 0xff;
    object.accelerationStructureReference = addressAS(dev, &ba);
    std::memcpy(instances.map, &object, sizeof object);
    VkAccelerationStructureGeometryKHR topGeometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    topGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    topGeometry.geometry.instances = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR, nullptr, VK_FALSE, {instances.address}};
    VkAccelerationStructureBuildGeometryInfoKHR top{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    top.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR; top.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    top.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR; top.geometryCount = 1; top.pGeometries = &topGeometry;
    const uint32_t one = 1;
    VkAccelerationStructureBuildSizesInfoKHR ts{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    sizesAS(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &top, &one, &ts);
    make_buffer(tlasStore, ts.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false);
    ac.buffer = tlasStore.b; ac.size = ts.accelerationStructureSize; ac.type = top.type;
    VkAccelerationStructureKHR tlas; vkcheck(createAS(dev, &ac, nullptr, &tlas), "TLAS"); top.dstAccelerationStructure = tlas;
    const VkDeviceSize alignment = std::max<VkDeviceSize>(asProps.minAccelerationStructureScratchOffsetAlignment, 1);
    make_buffer(scratch, std::max(bs.buildScratchSize, ts.buildScratchSize) + alignment, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
    bottom.scratchData.deviceAddress = top.scratchData.deviceAddress = (scratch.address + alignment - 1) / alignment * alignment;

    // ---- the emissive scene (case 7; layer/emissive.cpp's format): one BLAS of five draws (one geometry
    // each, the geometry index is the emInfo entry), coplanar with region REmit's floor (z = 0), one TLAS ----
    struct EmVertex { float p[3]; float pad; float uv[2]; float pad2[2]; };   // soup stride 32, uv at +16
    static_assert(sizeof(EmVertex) == 32);
    constexpr int kEmDraws = 5;
    const float ec = cx(REmit);
    const float emX[kEmDraws] = {ec, ec + 5, ec - 5, ec + 9, ec - 9}, emHalf[kEmDraws] = {2, 1, 1, 1, 1};
    std::vector<EmVertex> emVerts;
    for (int g = 0; g < kEmDraws; ++g) {
      const float x0 = emX[g] - emHalf[g], x1 = emX[g] + emHalf[g], y0 = kY - 2, y1 = kY + 2;
      const EmVertex q[4] = {{{x0, y0, 0}, 0, {0, 0}, {}}, {{x1, y0, 0}, 0, {1, 0}, {}}, {{x1, y1, 0}, 0, {1, 1}, {}}, {{x0, y1, 0}, 0, {0, 1}, {}}};
      for (int i : {0, 1, 2, 0, 2, 3}) emVerts.push_back(q[i]);
    }
    Buffer emSoupBuf, emInstances, emBlasStore, emTlasStore, emScratch;
    make_buffer(emSoupBuf, emVerts.size() * sizeof(EmVertex), inputUsage); std::memcpy(emSoupBuf.map, emVerts.data(), emVerts.size() * sizeof(EmVertex));
    make_buffer(emInstances, 64, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
    std::vector<VkAccelerationStructureGeometryKHR> emGeoms(kEmDraws, geometry);
    std::vector<uint32_t> emTris(kEmDraws, 2);
    for (int g = 0; g < kEmDraws; ++g) {
      auto& t = emGeoms[g].geometry.triangles;
      t.vertexData.deviceAddress = emSoupBuf.address + VkDeviceAddress(g) * 6 * sizeof(EmVertex);
      t.vertexStride = sizeof(EmVertex); t.maxVertex = 5;
    }
    VkAccelerationStructureBuildGeometryInfoKHR emBottom = bottom;
    emBottom.geometryCount = kEmDraws; emBottom.pGeometries = emGeoms.data(); emBottom.dstAccelerationStructure = VK_NULL_HANDLE;
    VkAccelerationStructureBuildSizesInfoKHR ebs{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    sizesAS(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &emBottom, emTris.data(), &ebs);
    make_buffer(emBlasStore, ebs.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false);
    ac.buffer = emBlasStore.b; ac.size = ebs.accelerationStructureSize; ac.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    VkAccelerationStructureKHR emBlas; vkcheck(createAS(dev, &ac, nullptr, &emBlas), "emissive BLAS"); emBottom.dstAccelerationStructure = emBlas;
    ba.accelerationStructure = emBlas;
    VkAccelerationStructureInstanceKHR emObject = object;
    emObject.accelerationStructureReference = addressAS(dev, &ba);
    std::memcpy(emInstances.map, &emObject, sizeof emObject);
    VkAccelerationStructureGeometryKHR emTopGeometry = topGeometry;
    emTopGeometry.geometry.instances.data.deviceAddress = emInstances.address;
    VkAccelerationStructureBuildGeometryInfoKHR emTop = top;
    emTop.pGeometries = &emTopGeometry; emTop.dstAccelerationStructure = VK_NULL_HANDLE;
    VkAccelerationStructureBuildSizesInfoKHR ets{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    sizesAS(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &emTop, &one, &ets);
    make_buffer(emTlasStore, ets.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false);
    ac.buffer = emTlasStore.b; ac.size = ets.accelerationStructureSize; ac.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    VkAccelerationStructureKHR emTlas; vkcheck(createAS(dev, &ac, nullptr, &emTlas), "emissive TLAS"); emTop.dstAccelerationStructure = emTlas;
    make_buffer(emScratch, std::max(ebs.buildScratchSize, ets.buildScratchSize) + alignment, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
    emBottom.scratchData.deviceAddress = emTop.scratchData.deviceAddress = (emScratch.address + alignment - 1) / alignment * alignment;
    // Records (em::EmRec, the host's layout): 0 constant, 1 x mask.r (texture 1), 2 approximate (not
    // calibrated), 3 approximate and calibrated, 4 excluded. InstInfo: base, stride | kEmInfoEmissive, record, triangles.
    Buffer emInfoBuf, emPoolBuf, emRecBuf, emLightBuf, emStatsBuf, emHeaderBuf, emGlobalBuf;
    make_buffer(emInfoBuf, 16 * kEmDraws, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT); make_buffer(emPoolBuf, 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    make_buffer(emRecBuf, sizeof(bg3::em::EmRec) * kEmDraws, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT); make_buffer(emLightBuf, 80, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    make_buffer(emStatsBuf, 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT); make_buffer(emHeaderBuf, 4096, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    make_buffer(emGlobalBuf, 64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    const float emRad[kEmDraws][3] = {{3, 2, 1}, {4, 4, 4}, {5, 5, 5}, {1, 0.5f, 0.25f}, {6, 6, 6}};
    const uint32_t emFlags[kEmDraws] = {0, 0, bg3::em::kFlagApprox, bg3::em::kFlagApprox | bg3::em::kFlagCalibrated, bg3::em::kFlagExcluded};
    for (int g = 0; g < kEmDraws; ++g) {
      bg3::em::EmRec r{};
      std::memcpy(r.radiance, emRad[g], 12);
      r.mode = g == 1 ? bg3::em::kModeMaskR : bg3::em::kModeConst;
      r.tex = g == 1 ? 1u : 0u;
      r.uv[0] = r.uv[1] = 1; r.uv2[0] = r.uv2[1] = 1;
      r.flags = emFlags[g]; r.glob = 0xFFFFFFFFu; r.draw = uint32_t(g);
      std::memcpy(static_cast<char*>(emRecBuf.map) + g * sizeof r, &r, sizeof r);
      const uint32_t info[4] = {uint32_t(g * 6 * sizeof(EmVertex)), uint32_t(sizeof(EmVertex)) | (1u << 11), uint32_t(g), 2u};
      std::memcpy(static_cast<char*>(emInfoBuf.map) + g * 16, info, 16);
    }
    { auto* h = static_cast<uint32_t*>(emHeaderBuf.map); h[2] = 1; h[3] = kEmDraws; h[7] = 1; }   // build camera (words 8-10) at 0

    // ---- resources of the four sets (the bindings spirv-reflect lists for the fixture SPIR-V) ----
    Buffer cam, viewport, stats, lflags, lscale, lclusters, lindex, llights, lconsts, cam2, cases, out;
    Buffer alphaRec, alphaSoup, instInfo, meshPool, stamp, boxes, atmo;
    constexpr uint32_t kMaxCases = 1u << 19;
    const auto ubo = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ssbo = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    make_buffer(cam, 400, ubo); make_buffer(viewport, 16, ubo); make_buffer(stats, 64 * 4 * 512, ssbo);
    make_buffer(lflags, 32, ubo); make_buffer(lscale, 16, ssbo); make_buffer(lclusters, 16, ssbo); make_buffer(lindex, 4 * 64, ssbo);
    make_buffer(llights, 80 * 32, ssbo); make_buffer(lconsts, 544, ubo); make_buffer(cam2, 224, ubo);
    make_buffer(cases, VkDeviceSize(kMaxCases) * sizeof(FxCase), ssbo); make_buffer(out, VkDeviceSize(kMaxCases) * sizeof(FxOut), ssbo);
    make_buffer(alphaRec, 256, ssbo); make_buffer(alphaSoup, 256, ssbo); make_buffer(instInfo, 16, ssbo); make_buffer(meshPool, 256, ssbo);
    make_buffer(stamp, 512, ssbo); make_buffer(boxes, 128 * 8, ssbo); make_buffer(atmo, 16, ubo);
    { auto* s = static_cast<uint32_t*>(stamp.map); s[0] = s[1] = s[2] = 1; }   // rt_scene_ok: a built, current scene
    { // Camera (gi_common.hlsli): frustum rays (-A,1,-A), (A,1,-A), (-A,1,A), viewport scale 0.5 bias 0, so
      // uv = ((x/y + A) / 2A, (z/y + A) / 2A): every scene point (y = 45..90) is on screen, depth = y.
      float* c = static_cast<float*>(cam.map); const float A = 100;
      const float rays[12] = {-A, 1, -A, 0, A, 1, -A, 0, -A, 1, A, 0}; std::memcpy(c + 28, rays, sizeof rays);
      const float v[4] = {0.5f, 0.5f, 0, 0}; std::memcpy(viewport.map, v, 16);
      // One froxel: tile size 1, 1 x 1 tiles, slice scale 0 / bias 0 (z = 0), last slice 0, resolution 1 x 1.
      float* lc = static_cast<float*>(lconsts.map); lc[30 * 4 + 2] = 1; lc[30 * 4 + 3] = 1;
      const uint32_t tile = 1; std::memcpy(lc + 31 * 4, &tile, 4);
      float* c2 = static_cast<float*>(cam2.map); c2[12 * 4 + 1] = 1;   // froxel depth scale; camera world position 0
      float* lf = static_cast<float*>(lflags.map); lf[0] = 1; lf[1] = 0.5f;   // m0.x AO used, m0.y AO-on-direct weight
      const uint32_t res[2] = {1, 1}; std::memcpy(lf + 4, res, 8);
      const float at[4] = {6360000, 6420000, 0, 0}; std::memcpy(atmo.map, at, 16); }
    // Images: a 1x1 RGBA8 (all sampled bindings), a 1x2 RGBA32UI storage image (hdSlots, unused: ls_flags bit 0 clear).
    auto make_image = [&](VkFormat format, VkImageUsageFlags usage, uint32_t w, uint32_t h, VkImage& image, VkDeviceMemory& mem) {
      VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
      ii.imageType = VK_IMAGE_TYPE_2D; ii.format = format; ii.extent = {w, h, 1}; ii.mipLevels = 1; ii.arrayLayers = 1;
      ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL; ii.usage = usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
      vkcheck(vkCreateImage(dev, &ii, nullptr, &image), "image");
      VkMemoryRequirements req; vkGetImageMemoryRequirements(dev, image, &req);
      VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = req.size;
      ai.memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      vkcheck(vkAllocateMemory(dev, &ai, nullptr, &mem), "image memory"); vkcheck(vkBindImageMemory(dev, image, mem, 0), "image bind");
    };
    VkImage texture, slots; VkDeviceMemory textureMem, slotsMem;
    make_image(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, 1, 1, texture, textureMem);
    make_image(VK_FORMAT_R32G32B32A32_UINT, VK_IMAGE_USAGE_STORAGE_BIT, 1, 2, slots, slotsMem);
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = texture; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = VK_FORMAT_R8G8B8A8_UNORM; vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView view, arrayView, slotsView;
    vkcheck(vkCreateImageView(dev, &vi, nullptr, &view), "view");
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY; vkcheck(vkCreateImageView(dev, &vi, nullptr, &arrayView), "array view");
    vi.image = slots; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = VK_FORMAT_R32G32B32A32_UINT; vkcheck(vkCreateImageView(dev, &vi, nullptr, &slotsView), "slots view");
    // The emissive mask (case 7): a 1x1 RGBA16F image, r = 0.25 (exact in FP16; any linear sample of one texel is it).
    VkImage mask; VkDeviceMemory maskMem; VkImageView maskView;
    make_image(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT, 1, 1, mask, maskMem);
    vi.image = mask; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = VK_FORMAT_R16G16B16A16_SFLOAT; vkcheck(vkCreateImageView(dev, &vi, nullptr, &maskView), "mask view");
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_LINEAR; si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSampler sampler; vkcheck(vkCreateSampler(dev, &si, nullptr, &sampler), "sampler");

    struct B { uint32_t set, binding; VkDescriptorType type; uint32_t count; };
    const VkDescriptorType U = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, S = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, I = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                           M = VK_DESCRIPTOR_TYPE_SAMPLER;
    const B bindings[] = {
        {0, 0, U, 1}, {0, 1, U, 1}, {0, 18, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1}, {0, 20, S, 1}, {0, 21, U, 1},
        {0, 22, S, 1}, {0, 23, S, 1}, {0, 24, S, 1}, {0, 25, S, 1}, {0, 26, U, 1}, {0, 29, U, 1},
        {0, 46, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}, {0, 60, S, 1}, {0, 61, S, 1},
        {1, 0, S, 1}, {1, 1, S, 1}, {1, 2, I, 4096}, {1, 3, I, 16}, {1, 4, M, 1}, {1, 5, M, 1}, {1, 6, S, 1}, {1, 7, S, 1}, {1, 8, S, 1}, {1, 9, S, 1},
        {2, 16, M, 1}, {2, 29, S, 1}, {2, 45, I, 1},
        {3, 0, U, 1}, {3, 1, I, 1}, {3, 2, I, 1}, {3, 3, I, 1}, {3, 4, I, 1}, {3, 5, I, 1}, {3, 6, M, 1},
        // the emissive scene (em_common.hlsli, EM_SET 4): the host's layout (common/hit_emission_bindings.h)
        {4, 0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1}, {4, 1, S, 1}, {4, 2, S, 1}, {4, 3, S, 1}, {4, 4, S, 1}, {4, 5, S, 1},
        {4, 6, I, 256}, {4, 7, M, 1}, {4, 8, S, 1}, {4, 9, S, 1}, {4, 10, S, 1}, {4, 11, I, 16}};
    constexpr uint32_t kSets = 5;
    VkDescriptorSetLayout setLayouts[kSets];
    std::vector<VkDescriptorPoolSize> poolSizes;
    for (uint32_t s = 0; s < kSets; ++s) {
      std::vector<VkDescriptorSetLayoutBinding> list;
      for (const B& b : bindings) if (b.set == s) {
        list.push_back({b.binding, b.type, b.count, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
        poolSizes.push_back({b.type, b.count});
      }
      VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
      dl.bindingCount = uint32_t(list.size()); dl.pBindings = list.data();
      vkcheck(vkCreateDescriptorSetLayout(dev, &dl, nullptr, setLayouts + s), "set layout");
    }
    VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp.maxSets = kSets; dp.poolSizeCount = uint32_t(poolSizes.size()); dp.pPoolSizes = poolSizes.data();
    VkDescriptorPool descriptorPool; vkcheck(vkCreateDescriptorPool(dev, &dp, nullptr, &descriptorPool), "descriptor pool");
    VkDescriptorSet sets[kSets];
    VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; da.descriptorPool = descriptorPool; da.descriptorSetCount = kSets; da.pSetLayouts = setLayouts;
    vkcheck(vkAllocateDescriptorSets(dev, &da, sets), "descriptor sets");
    auto writeBuffer = [&](uint32_t s, uint32_t b, const Buffer& buffer, VkDescriptorType type, VkDeviceSize range = VK_WHOLE_SIZE) {
      VkDescriptorBufferInfo bi{buffer.b, 0, range};
      VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstSet = sets[s]; w.dstBinding = b; w.descriptorCount = 1; w.descriptorType = type; w.pBufferInfo = &bi;
      vkUpdateDescriptorSets(dev, 1, &w, 0, nullptr);
    };
    auto writeImages = [&](uint32_t s, uint32_t b, VkDescriptorType type, uint32_t count, VkImageView v, VkImageLayout layout) {
      std::vector<VkDescriptorImageInfo> info(count, {type == M ? sampler : VK_NULL_HANDLE, type == M ? VK_NULL_HANDLE : v, layout});
      VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstSet = sets[s]; w.dstBinding = b; w.descriptorCount = count; w.descriptorType = type; w.pImageInfo = info.data();
      vkUpdateDescriptorSets(dev, 1, &w, 0, nullptr);
    };
    const auto RO = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    writeBuffer(0, 0, cam, U); writeBuffer(0, 1, viewport, U); writeBuffer(0, 20, stats, S); writeBuffer(0, 21, lflags, U);
    writeBuffer(0, 22, lscale, S); writeBuffer(0, 23, lclusters, S); writeBuffer(0, 24, lindex, S); writeBuffer(0, 25, llights, S);
    writeBuffer(0, 26, lconsts, U); writeBuffer(0, 29, cam2, U);
    writeImages(0, 46, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, slotsView, VK_IMAGE_LAYOUT_GENERAL);
    writeBuffer(1, 0, alphaRec, S); writeBuffer(1, 1, alphaSoup, S); writeBuffer(1, 6, alphaSoup, S); writeBuffer(1, 7, instInfo, S);
    writeBuffer(1, 8, meshPool, S); writeBuffer(1, 9, stamp, S);
    writeImages(1, 2, I, 4096, view, RO); writeImages(1, 3, I, 16, arrayView, RO); writeImages(1, 4, M, 1, view, RO); writeImages(1, 5, M, 1, view, RO);
    writeImages(2, 16, M, 1, view, RO); writeBuffer(2, 29, boxes, S); writeImages(2, 45, I, 1, view, RO);
    writeBuffer(3, 0, atmo, U); for (uint32_t b = 1; b <= 5; ++b) writeImages(3, b, I, 1, view, RO); writeImages(3, 6, M, 1, view, RO);
    writeBuffer(4, 1, emInfoBuf, S); writeBuffer(4, 2, emPoolBuf, S); writeBuffer(4, 3, emSoupBuf, S); writeBuffer(4, 4, emRecBuf, S);
    writeBuffer(4, 5, emLightBuf, S); writeBuffer(4, 8, emStatsBuf, S); writeBuffer(4, 9, emHeaderBuf, S); writeBuffer(4, 10, emGlobalBuf, S);
    writeImages(4, 6, I, 256, view, RO); writeImages(4, 7, M, 1, view, RO); writeImages(4, 11, I, 16, arrayView, RO);
    {   // emTex[1]: the mask
      VkDescriptorImageInfo mi{VK_NULL_HANDLE, maskView, RO};
      VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstSet = sets[4]; w.dstBinding = 6; w.dstArrayElement = 1; w.descriptorCount = 1;
      w.descriptorType = I; w.pImageInfo = &mi; vkUpdateDescriptorSets(dev, 1, &w, 0, nullptr);
    }
    {
      VkWriteDescriptorSetAccelerationStructureKHR aw{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR}; aw.accelerationStructureCount = 1; aw.pAccelerationStructures = &tlas;
      VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, &aw}; w.dstSet = sets[0]; w.dstBinding = 18; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
      vkUpdateDescriptorSets(dev, 1, &w, 0, nullptr);
      aw.pAccelerationStructures = &emTlas; w.dstSet = sets[4]; w.dstBinding = 0;
      vkUpdateDescriptorSets(dev, 1, &w, 0, nullptr);
    }
    VkPushConstantRange pushRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = kSets; pl.pSetLayouts = setLayouts; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &pushRange;
    VkPipelineLayout layout; vkcheck(vkCreatePipelineLayout(dev, &pl, nullptr, &layout), "pipeline layout");
    VkPipeline pipeline;
    {
      auto code = read_shader(argv[1]);
      VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; ci.codeSize = code.size() * 4; ci.pCode = code.data();
      VkShaderModule m; vkcheck(vkCreateShaderModule(dev, &ci, nullptr, &m), "shader module");
      VkComputePipelineCreateInfo p{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
      p.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, m, "main"}; p.layout = layout;
      vkcheck(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &p, nullptr, &pipeline), "compute pipeline");
      vkDestroyShaderModule(dev, m, nullptr);
    }
    VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; cp.queueFamilyIndex = family;
    VkCommandPool commandPool; vkcheck(vkCreateCommandPool(dev, &cp, nullptr, &commandPool), "command pool");
    VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ca.commandPool = commandPool; ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ca.commandBufferCount = 1;
    VkCommandBuffer cmd; vkcheck(vkAllocateCommandBuffers(dev, &ca, &cmd), "command buffer");
    auto submit = [&](const std::function<void()>& record) {
      vkcheck(vkResetCommandBuffer(cmd, 0), "reset");
      VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      vkcheck(vkBeginCommandBuffer(cmd, &begin), "begin"); record(); vkcheck(vkEndCommandBuffer(cmd), "end");
      VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO}; s.commandBufferCount = 1; s.pCommandBuffers = &cmd;
      vkcheck(vkQueueSubmit(queue, 1, &s, VK_NULL_HANDLE), "submit"); vkcheck(vkQueueWaitIdle(queue), "wait");
    };
    submit([&] {   // build the scene, initialise the images
      barrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_WRITE_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_SHADER_READ_BIT);
      VkAccelerationStructureBuildRangeInfoKHR br{triangles, 0, 0, 0}, tr{1, 0, 0, 0};
      const auto* bp = &br; const auto* tp = &tr;
      buildAS(cmd, 1, &bottom, &bp);
      std::vector<VkAccelerationStructureBuildRangeInfoKHR> ebr(kEmDraws, VkAccelerationStructureBuildRangeInfoKHR{2, 0, 0, 0});
      const auto* ebp = ebr.data();
      buildAS(cmd, 1, &emBottom, &ebp);
      barrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
              VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);
      buildAS(cmd, 1, &top, &tp);
      buildAS(cmd, 1, &emTop, &tp);
      VkImageMemoryBarrier ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      ib.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; ib.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      VkImageMemoryBarrier two[3] = {ib, ib, ib}; two[0].image = texture; two[1].image = slots; two[2].image = mask;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 3, two);
      VkClearColorValue white{{1, 1, 1, 1}}, zero{}, quarter{{0.25f, 0.5f, 0.75f, 1}}; vkCmdClearColorImage(cmd, texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &white, 1, &ib.subresourceRange);
      vkCmdClearColorImage(cmd, slots, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &ib.subresourceRange);
      vkCmdClearColorImage(cmd, mask, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &quarter, 1, &ib.subresourceRange);
      for (auto& b : two) { b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; }
      two[0].newLayout = RO; two[1].newLayout = VK_IMAGE_LAYOUT_GENERAL; two[2].newLayout = RO;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 3, two);
    });
    double gpuSeconds = 0;
    // One dispatch: upload its lights, boxes, sun and cases; run; return the results.
    auto run = [&](const Dispatch& d) {
      check(d.cases.size() <= kMaxCases && d.lights.size() <= 32 && d.boxes.size() <= 8, "dispatch size");
      std::memset(llights.map, 0, size_t(llights.size)); std::memcpy(llights.map, d.lights.data(), d.lights.size() * sizeof(Light));
      std::memset(boxes.map, 0, size_t(boxes.size)); std::memcpy(boxes.map, d.boxes.data(), d.boxes.size() * sizeof(Box));
      auto* index = static_cast<uint32_t*>(lindex.map);
      std::memset(index, 0, size_t(lindex.size));
      for (uint32_t i = 0; i < d.lights.size(); ++i) index[i] = i;
      for (uint32_t i = 0; i < d.boxes.size(); ++i) index[d.lights.size() + i] = i;
      const uint32_t cluster[4] = {0, d.points | (d.spots << 12), uint32_t(d.boxes.size()), 0};
      check(d.points + d.spots == d.lights.size(), "points + spots");
      std::memcpy(lclusters.map, cluster, 16);
      std::memcpy(lscale.map, &d.global, 4);
      float* c = static_cast<float*>(cam.map);
      std::memcpy(c + 0, d.sun_dir, 12); std::memcpy(c + 4, d.sun_col, 12);   // v0 towards the sun, v1 its colour (x[5].x = 0: no LUT)
      std::memcpy(cases.map, d.cases.data(), d.cases.size() * sizeof(FxCase));
      std::memset(out.map, 0xcd, d.cases.size() * sizeof(FxOut));
      writeBuffer(0, 60, cases, S, d.cases.size() * sizeof(FxCase));
      writeBuffer(0, 61, out, S, d.cases.size() * sizeof(FxOut));
      const auto t0 = std::chrono::steady_clock::now();
      submit([&] {
        barrier(cmd, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_UNIFORM_READ_BIT);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, kSets, sets, 0, nullptr);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push), &d.push);
        vkCmdDispatch(cmd, uint32_t((d.cases.size() + 63) / 64), 1, 1);
        barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
      });
      gpuSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
      std::vector<FxOut> r(d.cases.size());
      std::memcpy(r.data(), out.map, r.size() * sizeof(FxOut));
      for (auto& x : r) check(x.mstatus == 0, "synthetic class-0 material qualifies");
      return r;
    };
    // Deterministic comparison of one radiance against its reference (component-wise).
    struct Cmp { double worst = 0; bool ok = true; int n = 0; };
    auto compare = [&](Cmp& cmp, const float* gpu, uint32_t status, D3 ref, uint32_t want = kHdOk) {
      ++cmp.n;
      if (status != want) { cmp.ok = false; std::printf("  status %u (want %u)\n", status, want); return; }
      const double r[3] = {ref.x, ref.y, ref.z};
      for (int i = 0; i < 3; ++i) {
        const double e = std::abs(gpu[i] - r[i]);
        if (r[i] == 0 ? gpu[i] != 0.0f : e > tol(r[i])) { cmp.ok = false; std::printf("  gpu %.9g ref %.9g\n", gpu[i], r[i]); }
        if (r[i] != 0) cmp.worst = std::max(cmp.worst, e / std::abs(r[i]));
      }
    };
    auto direct_ref = [&](const Dispatch& d, const FxCase& c) {   // unshadowed point/spot sum, the reference of a lit point
      D3 sum{0, 0, 0}; const D3 P = f3(c.p), wo = f3(c.v);
      for (uint32_t i = 0; i < d.lights.size(); ++i) {
        D3 L; const D3 E = light_incident(d.lights[i], i >= d.points, P, c.channel, d.global, L);
        sum = sum + mul(fresnel_blend(case_mat(c), kN, wo, L), E) * std::max(0.0, dot(kN, L));
      }
      return sum;
    };
    auto make_light = [](Vec p, float radius, D3 col, float m3, float m4, float m5) {
      Light l{}; l.pos[0] = p.x; l.pos[1] = p.y; l.pos[2] = p.z; l.inv_r = 1.0f / radius;
      l.col[0] = float(col.x); l.col[1] = float(col.y); l.col[2] = float(col.z); l.m3 = m3; l.m4 = m4; l.m5 = m5; l.chan = 1; l.strength = 1;
      return l;
    };

    // ---- 1. point / spot light above a plane vs bg3_light_eval's falloff and the declared BSDF ----
    {
      const float c = cx(RPoint);
      const float offs[] = {0, 0.5f, 1, 2, 3, 4, 5, 5.5f, 5.9f, 6.5f};
      struct Variant { const char* name; Light light; bool spot; };
      Light spot = make_light({c, kY, 3}, 8, {4, 3, 2}, 1, 1.5f, 0.1f);
      spot.dir[2] = -1; spot.cos_outer = float(std::cos(35 * kPi / 180)); spot.cos_inner = float(std::cos(15 * kPi / 180));
      const Variant variants[] = {
          {"1a point inverse-square m4=2 r=6", make_light({c, kY, 2}, 6, {3, 2, 1}, 1, 2, 0), false},
          {"1b point m5<0 model, x b13[0], m4=1", make_light({c, kY, 2}, 6, {3, 2, 1}, -1, 1, -0.3f), false},
          {"1c spot cone 15/35 deg m4=1.5", spot, true}};
      for (const Variant& v : variants) {
        Dispatch d; d.push = default_push(); d.lights = {v.light}; (v.spot ? d.spots : d.points) = 1;
        for (float o : offs) for (int dir = 0; dir < 2; ++dir)
          d.cases.push_back(make_case({c + o * (dir ? 0.6f : 1.0f), kY + o * (dir ? 0.8f : 0.0f), 0}, kFxDirect));
        d.cases.push_back(make_case({c + 1, kY, 0}, kFxDirect, kMat, 3));   // channel 3: the light's mask 1 excludes it
        auto r = run(d);
        Cmp cmp; int lit = 0, zero = 0;
        for (size_t i = 0; i < r.size(); ++i) {
          const D3 ref = direct_ref(d, d.cases[i]);
          compare(cmp, r[i].a, r[i].sa, ref);
          (ref.x > 0 ? lit : zero) += 1;
        }
        report.line(v.name, cmp.ok, fmt("%.0f lit + %.0f exact-zero (range/cone/channel), worst rel %.2e", lit, zero, cmp.worst));
      }
      // Light inside a closed 0.2 m lamp cube: the stop-short rule lets it light; with no stop-short it is occluded.
      const float l = cx(RLamp);
      for (int control = 0; control < 2; ++control) {
        Dispatch d; d.push = default_push(); d.lights = {make_light({l, kY, 2}, 6, {3, 2, 1}, 1, 2, 0)}; d.points = 1;
        if (control) { d.push.ls_clear = 0; d.push.ls_source_radius = 0; }
        for (float o : {0.0f, 1.0f, 2.5f, 4.0f}) d.cases.push_back(make_case({l + o, kY + 0.3f * o, 0}, kFxDirect));
        auto r = run(d);
        Cmp cmp;
        for (size_t i = 0; i < r.size(); ++i) compare(cmp, r[i].a, r[i].sa, control ? D3{0, 0, 0} : direct_ref(d, d.cases[i]));
        report.line(control ? "1e lamp control: ls_clear 0, R 0 -> occluded" : "1d light inside a closed lamp mesh: lit",
                    cmp.ok, fmt("%.0f points, worst rel %.2e", cmp.n, cmp.worst));
      }
    }
    // ---- 2. occluders: between (occluded), behind the light (lit), within ls_clear (lit, by the rule) ----
    {
      const float c = cx(ROcc);
      for (int game = 0; game < 2; ++game) {
        Dispatch d; d.push = default_push(); d.lights = {make_light({c, kY, 3}, 8, {3, 2, 1}, 1, 2, 0)}; d.points = 1;
        if (game) d.push.ls_min_radius = 100;   // the light (r 8, no shadow map) is left at the game's visibility
        const Vec pts[] = {{c + 4, kY, 0}, {c - 2, kY, 0}, {c, kY, 0}, {c - 6, kY, 0}};
        const bool occluded[] = {!game, false, false, false};
        for (Vec p : pts) d.cases.push_back(make_case(p, kFxDirect));
        auto r = run(d);
        Cmp cmp; bool nativeOk = true;
        for (size_t i = 0; i < r.size(); ++i) {
          const D3 ref = occluded[i] ? D3{0, 0, 0} : direct_ref(d, d.cases[i]);
          check(occluded[i] || ref.x > 0, "occluder fixture point in range");
          compare(cmp, r[i].a, r[i].sa, ref);
          for (int k = 0; k < 3; ++k) nativeOk &= game ? r[i].c[k] == r[i].a[k] : r[i].c[k] == 0.0f;
        }
        report.line(game ? "2b game-visibility light (r < ls_min_radius)" : "2a occluder between / behind / in ls_clear",
                    cmp.ok && nativeOk, game ? fmt("unshadowed through A, .native == radiance (%.0f pts)", cmp.n)
                                             : fmt("A occludes P1; ceiling B behind light lit; C in 0.2 m lit; worst rel %.2e", cmp.worst));
      }
    }
    // ---- 3. sun: open plane lit with N.L, roof slab occluded, disc sampling energy ----
    {
      const D3 L = unit(D3{0.6, 0, 0.8});
      Dispatch d; d.push = default_push(); d.global = 0.7f;
      d.sun_dir[0] = 0.6f; d.sun_dir[2] = 0.8f; d.sun_col[0] = 5; d.sun_col[1] = 4; d.sun_col[2] = 3;
      for (float o : {-10.0f, -3.0f, 0.0f, 4.0f, 10.0f}) {
        d.cases.push_back(make_case({cx(RSunOpen) + o, kY + 0.5f * o, 0}, kFxSun, kMat, 0, uint32_t(d.cases.size())));
        d.cases.push_back(make_case({cx(RRoof) + o, kY + 0.5f * o, 0}, kFxSun, kMat, 0, uint32_t(d.cases.size())));
      }
      auto r = run(d);
      Cmp open, roof;
      const D3 sunE = D3{5, 4, 3} * 0.7;
      for (size_t i = 0; i < r.size(); ++i) {
        const FxCase& cs = d.cases[i];
        const D3 lit = mul(fresnel_blend(case_mat(cs), kN, f3(cs.v), L), sunE) * dot(kN, L);
        compare(i % 2 ? roof : open, r[i].b, r[i].sb, i % 2 ? D3{0, 0, 0} : lit);
      }
      report.line("3a sun open plane (L.N 0.8, b13[0] 0.7)", open.ok, fmt("%.0f pts, worst rel %.2e", open.n, open.worst));
      report.line("3b sun under a roof slab", roof.ok, fmt("%.0f pts exact zero", roof.n));
      // Disc: L = +z, sun_radius 0.02, half-plane at height 20 with its edge s from the axis. The sample is
      // uniform in a tangent disc, so on the plane it is uniform in a disc of radius 20 tan(0.02): the
      // occluded fraction is the circular segment beyond s.
      Dispatch e; e.push = default_push(); e.push.sun_radius = 0.02f; e.global = 1;
      e.sun_dir[2] = 1; e.sun_col[0] = e.sun_col[1] = e.sun_col[2] = 2;
      const float ss[] = {0.0f, 0.2f, 1.0f};
      constexpr uint32_t kSamples = 1u << 16;
      for (float s : ss) for (uint32_t i = 0; i < kSamples; ++i) e.cases.push_back(make_case({cx(RDisc) - s, kY, 0}, kFxSun, kMat, 0, 7));
      auto q = run(e);
      const double rho = (20.0 - 0.0) * std::tan(double(0.02f));
      for (int g = 0; g < 3; ++g) {
        const FxCase& cs = e.cases[size_t(g) * kSamples];
        const D3 lit = mul(fresnel_blend(case_mat(cs), kN, f3(cs.v), {0, 0, 1}), {2, 2, 2});
        double sum = 0; bool binary = true;
        for (uint32_t i = 0; i < kSamples; ++i) {
          const FxOut& o = q[size_t(g) * kSamples + i];
          binary &= o.sb == kHdOk && (o.b[0] == 0.0f || std::abs(o.b[0] - lit.x) <= tol(lit.x));
          sum += o.b[0] / lit.x;
        }
        const double s = ss[g], x = std::min(s / rho, 1.0);
        const double occluded = x >= 1 ? 0 : (std::acos(x) - x * std::sqrt(1 - x * x)) / kPi;
        const double want = 1 - occluded, got = sum / kSamples, sigma = std::sqrt(want * (1 - want) / kSamples);
        const double bound = 5 * sigma + std::ldexp(1.0, -11);
        report.line(g == 0 ? "3c sun disc, edge on axis" : g == 1 ? "3d sun disc, edge at rho/2" : "3e sun disc, edge outside",
                    binary && std::abs(got - want) <= bound, fmt("visible %.5f analytic %.5f (5 sigma %.1e)", got, want, bound));
      }
      // The disc sample's own cone (native_hit_sun_dir): every direction within sun_radius of L, and the
      // largest angle of 2^16 samples reaches it (sqrt(u1) <= 1: max ratio 1 - O(1e-5)). With |L.y| >= 0.99
      // its tangent used to be t = (1,0,0), not orthogonal to L, which shrank the disc by |t x L| = sqrt(1 - L.x^2)
      // (printed as FINDING until the frame became normalize(cross((0,0,1), L)), 2026-11-01): now a gate.
      for (int zenith = 0; zenith < 2; ++zenith) {
        Dispatch g; g.push = default_push(); g.push.sun_radius = 0.02f;
        const D3 Lz = zenith ? unit(D3{0.12, 0.99, 0.0}) : D3{0, 0, 1};
        g.sun_dir[0] = float(Lz.x); g.sun_dir[1] = float(Lz.y); g.sun_dir[2] = float(Lz.z);
        for (uint32_t i = 0; i < kSamples; ++i) g.cases.push_back(make_case({cx(RDisc), kY, 0}, kFxSunDir, kMat, 0, 9));
        auto w = run(g);
        const D3 Lf = unit(f3(g.sun_dir));
        double maxRatio = 0; bool inside = true;
        for (const FxOut& o : w) {
          const double ang = std::acos(std::clamp(dot(unit(f3(o.b)), Lf), -1.0, 1.0)) / double(0.02f);
          maxRatio = std::max(maxRatio, ang); inside &= ang <= 1 + 1e-3;
        }
        // Angular resolution of the FP32 direction at 0.02 rad: ~1e-7 / 0.02; the extreme of 2^16 uniform
        // sqrt(u1) samples is 1 - 1/(2 x 2^16) on average; 1e-3 is far beyond both.
        const bool ok = inside && maxRatio >= 1 - 1e-3;
        const std::string detail = fmt("L.y %.3f: max angle / sun_radius %.5f (want 1)", Lz.y, maxRatio);
        report.line(zenith ? "3g sun disc frame near zenith (|L.y|>=0.99)" : "3f sun disc sample cone (L +z)", ok, detail);
      }
    }
    // ---- 4. box (projector) light: inside/outside, ramps, direction _m9, its ray only with a shadow map ----
    {
      // 4c: the box term's AO-on-direct share (native_hit_direct.hlsli hd_box_game). With the screen's box
      // pass off (ls_flags bit 26 clear: the game keeps box diffuse x mix(1, AO, m0.y)) every box term is in
      // .native, bit for bit; with it on (bit 26) .native is 0 and the radiance is unchanged, bit for bit.
      bool boxNativeOk = true; int boxNativeN = 0;
      for (int shadow = 0; shadow < 2; ++shadow) {
        const float c = cx(shadow ? RBoxShadow : RBox);
        Box b{}; b.col[0] = 2; b.col[1] = 1.5f; b.col[2] = 1; b.m1 = 1;   // x b13[0], no cookie
        b.ramp[0] = 0.2f; b.ramp[1] = 0.8f; b.ramp[2] = 5; b.ramp[3] = 5; b.edge[0] = b.edge[1] = 4;
        b.m4 = 0xffffffu | ((shadow ? 3u : 2u) << 24); b.strength = 1;
        const float x0 = c - 4, y0 = kY - 4, z0 = -0.5f;   // box [c-4, c+4] x [56, 64] x [-0.5, 4.5]
        b.r0[0] = 1 / 8.0f; b.r0[3] = -x0 / 8; b.r1[1] = 1 / 8.0f; b.r1[3] = -y0 / 8; b.r2[2] = 1 / 5.0f; b.r2[3] = -z0 / 5;
        b.flags = shadow ? 1u : 0u; b.dir[0] = 0.6f; b.dir[2] = 0.8f;
        Dispatch d; d.push = default_push(); d.global = 0.7f; d.boxes = {b};
        std::vector<Vec> pts = {{c, kY, 0}, {c - 3.2f, kY + 1, 0}, {c - 3.76f, kY + 1, 0}, {c + 3.6f, kY, 0}, {c, kY - 2.8f, 0},
                                {c + 4.4f, kY, 0}, {c, kY + 4.2f, 0}};
        std::vector<bool> occl(pts.size(), false);
        // P: D (z 2) is on its path inside the box; Q: E (z 6) is on its path beyond the box's top; R: clear.
        pts.push_back({c - 3, kY, 0}); occl.push_back(shadow != 0);
        pts.push_back({c - 0.5f, kY, 0}); occl.push_back(false);
        pts.push_back({c + 2, kY, 0}); occl.push_back(false);
        for (Vec p : pts) d.cases.push_back(make_case(p, kFxDirect));
        auto r = run(d);
        Dispatch dt = d; dt.push.ls_flags |= 1u << 26;
        auto rt = run(dt);
        for (size_t i = 0; i < r.size(); ++i) {
          ++boxNativeN;
          for (int k = 0; k < 3; ++k)
            boxNativeOk &= r[i].c[k] == r[i].a[k] && rt[i].c[k] == 0.0f && rt[i].a[k] == r[i].a[k] && rt[i].sa == r[i].sa;
        }
        Cmp cmp; int inside = 0;
        const D3 Ld = unit(f3(b.dir));
        for (size_t i = 0; i < r.size(); ++i) {
          const FxCase& cs = d.cases[i];
          const D3 E = box_incident(b, f3(cs.p), 0, d.global);
          inside += E.x > 0;
          compare(cmp, r[i].a, r[i].sa, occl[i] ? D3{0, 0, 0} : mul(fresnel_blend(case_mat(cs), kN, f3(cs.v), Ld), E) * dot(kN, Ld));
        }
        report.line(shadow ? "4b box _m8 bit0: ray to the box exit" : "4a box unshadowed (game look), smoothstep",
                    cmp.ok, fmt(shadow ? "%.0f inside, D occludes, E beyond exit lit, worst rel %.2e" : "%.0f inside, 2 outside, D ignored, worst rel %.2e",
                                inside, cmp.worst));
      }
      report.line("4c box term x AO factor unless D-LS boxes", boxNativeOk,
                  fmt("%.0f pts: .native = box term (bit 26 clear), 0 with bit 26", boxNativeN));
    }
    // ---- 5. AO: open plane 1, corner the hybrid GI rule's expectation ----
    {
      Dispatch d; d.push = default_push(); d.push.gi_rt_radius = 1; d.push.rt_ao_rays = 8;
      const double R = 1, hs[] = {0.1, 0.5};
      constexpr uint32_t kSamples = 1u << 14;
      for (uint32_t i = 0; i < kSamples; ++i) d.cases.push_back(make_case({cx(RAoOpen), kY, 0}, kFxAo, kMat, 0, 11));
      for (double h : hs) for (uint32_t i = 0; i < kSamples; ++i) d.cases.push_back(make_case({cx(RCorner) - float(h), kY, 0}, kFxAo, kMat, 0, 13));
      auto r = run(d);
      bool openOk = true;
      for (uint32_t i = 0; i < kSamples; ++i) openOk &= r[i].ao == 1.0f && r[i].factor == 1.0f;
      report.line("5a AO open plane", openOk, "every estimate exactly 1, factor 1");
      for (int g = 0; g < 2; ++g) {
        // Wall at distance h, cosine rays about +z: E[w] = (1/pi) int_{-pi/2}^{pi/2} g1(a) dphi, a = h / (R cos phi),
        // g1 = (1 - a^2 + 2 a^2 ln a) / 2 (a < 1); E[w^2] with g2 = (1 - a^4 + 4 a^2 ln a) / 2.
        const double h = hs[g];
        double e1 = 0, e2 = 0; const int steps = 200000;
        for (int k = 0; k < steps; ++k) {
          const double phi = -kPi / 2 + (k + 0.5) * kPi / steps, a = h / (R * std::cos(phi));
          if (a >= 1) continue;
          e1 += (1 - a * a + 2 * a * a * std::log(a)) / 2; e2 += (1 - a * a * a * a + 4 * a * a * std::log(a)) / 2;
        }
        e1 *= 1 / double(steps); e2 *= 1 / double(steps);   // (1/pi) x (pi / steps)
        double sum = 0, fsum = 0; bool factorOk = true;
        for (uint32_t i = 0; i < kSamples; ++i) {
          const FxOut& o = r[size_t(g + 1) * kSamples + i];
          sum += o.ao; fsum += o.factor;
          factorOk &= std::abs(o.factor - (1 + (o.ao - 1) * 0.5)) <= 1e-6;
        }
        const double want = 1 - e1, got = sum / kSamples, sigma = std::sqrt((e2 - e1 * e1) / (8.0 * kSamples)), bound = 5 * sigma + 1e-6;
        report.line(g ? "5c AO corner, wall at 0.5 m (R 1)" : "5b AO corner, wall at 0.1 m (R 1)", factorOk && std::abs(got - want) <= bound,
                    fmt("A %.5f analytic %.5f (5 sigma %.1e)", got, want, bound));
      }
    }
    // ---- 6. energy: direct + sun on a white plane is additive; white furnace of the shared BSDF <= 1 ----
    {
      const Mat white{{1, 1, 1}, 0.5, 0, 0.5};
      const float c = cx(REnergy);
      Dispatch d; d.push = default_push(); d.global = 0.7f;
      d.lights = {make_light({c, kY, 2}, 6, {3, 2, 1}, 1, 2, 0)}; d.points = 1;
      d.sun_dir[0] = 0.6f; d.sun_dir[2] = 0.8f; d.sun_col[0] = 5; d.sun_col[1] = 4; d.sun_col[2] = 3;
      for (float o : {0.0f, 1.0f, 2.0f, 4.0f}) d.cases.push_back(make_case({c + o, kY, 0}, kFxCombined, white));
      auto r = run(d);
      Cmp cmp; const D3 L = unit(D3{0.6, 0, 0.8});
      for (size_t i = 0; i < r.size(); ++i) {
        const FxCase& cs = d.cases[i];
        const D3 sun = mul(fresnel_blend(white, kN, f3(cs.v), L), D3{5, 4, 3} * 0.7) * dot(kN, L);
        const D3 ref = direct_ref(d, cs) + sun;
        const float sumGpu[3] = {r[i].a[0] + r[i].b[0], r[i].a[1] + r[i].b[1], r[i].a[2] + r[i].b[2]};
        compare(cmp, sumGpu, r[i].sa == kHdOk && r[i].sb == kHdOk ? kHdOk : 99u, ref);
      }
      report.line("6a direct + sun (zero fallback) additive", cmp.ok, fmt("%.0f pts, worst rel %.2e", cmp.n, cmp.worst));
      // White furnace: albedo(wo) = int f cos dw over the hemisphere by the midpoint rule in (theta, phi), at
      // two resolutions; the quadrature error estimate is their difference.
      Dispatch f; f.push = default_push();
      const double rough[] = {0.3, 0.6, 1.0}, theta[] = {0, 60, 85};
      const uint32_t res[2][2] = {{128, 64}, {256, 128}};
      for (double rg : rough) for (double t : theta) for (auto& rr : res)
        for (uint32_t j = 0; j < rr[0]; ++j) for (uint32_t k = 0; k < rr[1]; ++k) {
          const double th = (j + 0.5) * (kPi / 2) / rr[0], ph = (k + 0.5) * 2 * kPi / rr[1];
          FxCase cs = make_case({0, kY, 0}, kFxFurnace, {{1, 1, 1}, rg, 0, 0.5});
          const double to = t * kPi / 180;
          cs.v[0] = float(std::sin(to)); cs.v[1] = 0; cs.v[2] = float(std::cos(to));
          cs.c48[0] = float(std::sin(th) * std::cos(ph)); cs.c48[1] = float(std::sin(th) * std::sin(ph)); cs.c48[2] = float(std::cos(th));
          f.cases.push_back(cs);
        }
      auto q = run(f);
      size_t at = 0; double worstAlbedo = 0; bool furnaceOk = true; Cmp bsdf; std::string worst;
      for (double rg : rough) for (double t : theta) {
        double albedo[2] = {0, 0};
        for (int k = 0; k < 2; ++k) {
          const uint32_t nt = res[k][0], nphi = res[k][1];
          for (uint32_t j = 0; j < nt; ++j) for (uint32_t m = 0; m < nphi; ++m, ++at) {
            const double th = (j + 0.5) * (kPi / 2) / nt;
            const FxCase& cs = f.cases[at];
            const D3 ref = fresnel_blend({{1, 1, 1}, cs.rough, 0, cs.refl}, kN, f3(cs.v), f3(cs.c48)) * f3(cs.c48).z;
            compare(bsdf, q[at].a, q[at].sa, ref);
            albedo[k] += q[at].a[0] * std::sin(th) * (kPi / 2 / nt) * (2 * kPi / nphi);
          }
        }
        const double quad = std::abs(albedo[1] - albedo[0]);
        furnaceOk &= albedo[1] <= 1 + quad + std::ldexp(1.0, -11);
        if (albedo[1] > worstAlbedo) { worstAlbedo = albedo[1]; worst = fmt("r %.1f wo %.0f deg: %.4f", rg, t, albedo[1]); }
      }
      report.line("6b hd_scatter == declared FresnelBlend", bsdf.ok, fmt("%.0f directions, worst rel %.2e", bsdf.n, bsdf.worst));
      report.line("6c white furnace albedo <= 1", furnaceOk, "max " + worst);
    }
    // ---- 7. emission at hits (rt_hit_emission): native_hit_emission over the emissive scene ----
    {
      Dispatch d; d.push = default_push();
      d.push.rt_ao_radius = 0.01f;   // HIT_EM_TOL = rt_em_tol (bg3rtx.ini)
      // A ray straight down from z = 3 onto region REmit's floor (z = 0): the main TLAS hit is the floor,
      // the emissive quad lies on it (the coplanar duplicate the host captures).
      auto down = [&](float x, float y, Vec cam = {0, 0, 0}) {
        FxCase c = make_case({x, y, 3}, kFxEmission);
        c.v[0] = 0; c.v[1] = 0; c.v[2] = -1; c.footprint = 0.01f;
        c.c48[0] = cam.x; c.c48[1] = cam.y; c.c48[2] = cam.z;
        return c;
      };
      struct Want { const char* group; uint32_t status, rec; D3 L; };
      std::vector<Want> want;
      auto add = [&](const FxCase& c, const char* g, uint32_t st, uint32_t rec, D3 L) { d.cases.push_back(c); want.push_back({g, st, rec, L}); };
      const D3 none{0, 0, 0};
      for (float x : {-1.5f, -0.5f, 0.0f}) for (float y : {-1.5f, 0.0f, 1.5f})
        add(down(ec + x, kY + y), "7a", kHeUsed, 0, {3, 2, 1});                  // constant record: its radiance exactly
      for (float x : {-0.5f, 0.5f}) add(down(ec + 5 + x, kY + 1), "7b", kHeUsed, 1, {1, 1, 1});   // x mask.r 0.25
      add(down(ec - 5, kY), "7c", kHeUnvalidated, 2, none);                         // approximate, not calibrated
      add(down(ec + 9, kY), "7c", kHeUsed, 3, {1, 0.5, 0.25});                      // approximate, calibrated
      add(down(ec - 9, kY), "7c", kHeExcluded, 4, none);                            // excluded family
      add(down(ec + 12, kY), "7d", kHeNone, 0xFFFFFFFFu, none);                     // floor, no emitter
      add(down(ec + 1, kY), "7d", kHeNone, 0xFFFFFFFFu, none);                      // occluder F in front (t 0.5)
      // em_shift: the scene was built with the camera at 0; the camera is now at +0.5 x, so a point of this
      // frame is 0.5 further along x in the scene: x = ec - 2.3 meets the quad (ec - 1.8), ec + 1.7 leaves it.
      add(down(ec - 2.3f, kY, {0.5f, 0, 0}), "7e", kHeUsed, 0, {3, 2, 1});
      add(down(ec + 1.7f, kY - 1.5f, {0.5f, 0, 0}), "7e", kHeNone, 0xFFFFFFFFu, none);
      add(down(ec - 2.3f, kY), "7e", kHeNone, 0xFFFFFFFFu, none);                   // the same ray without the shift
      auto r = run(d);
      const char* groups[] = {"7a", "7b", "7c", "7d", "7e"};
      const char* names[] = {"7a emissive quad: the record's radiance", "7b emissive quad: radiance x mask.r",
                             "7c record policy (approx/calibrated/excluded)", "7d no emitter / occluder in front: 0",
                             "7e latency shift em_shift (sign)"};
      for (int gi = 0; gi < 5; ++gi) {
        Cmp cmp; bool ok = true; int n = 0;
        for (size_t i = 0; i < r.size(); ++i) {
          if (std::strcmp(want[i].group, groups[gi]) != 0) continue;
          ++n;
          compare(cmp, r[i].a, r[i].sa, want[i].L, want[i].status);
          if (r[i].sb != want[i].rec) { ok = false; std::printf("  record %u (want %u)\n", r[i].sb, want[i].rec); }
          if (r[i].b[1] != 0.0f) { ok = false; std::printf("  main query aborted\n"); }
        }
        report.line(names[gi], ok && cmp.ok && n > 0, fmt("%.0f rays, worst rel %.2e", n, cmp.worst));
      }
    }
    // ---- 8. D-LS exact-k selection (ls_k, common/ls_exact_k.h as gi/rt_local_shadows*_k.hlsl runs it) ----
    // Six light weights per case, K 1, 2 and 4, NR 1-2, the uniforms on a midpoint grid of N cells (the second an
    // odd-multiplier permutation of it). Analytic references: the exact set is the K largest weights in list order
    // (8a, exact); sample j is drawn with p_j = w_j / sum_R w and the estimator's grid mean is the full sum (8b, 8c);
    // a sample's scaled weight never exceeds sum_R w (8c); the GPU's selection equals the C++ header's (8d).
    // Grid bound: every reservoir decision resolves its boundary to one cell of the rescaled uniform, so a
    // probability over |R| decisions is off by at most (|R| + 1) / N. GPU and C++ may differ only in cells that
    // straddle a boundary (FP32 division of the rescale): at most one per decision and uniform, 2 |R|.
    {
      namespace lk = bg3::ls_exact_k;
      const float sets[][6] = {{0.5f, 3.0f, 0.25f, 1.5f, 0.0f, 2.0f}, {7.0f, 0.01f, 0.02f, 0.03f, 5.0f, 0.0f},
                               {4.0f, 1.0f, 9.0f, 0.5f, 0.75f, 3.0f}, {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
      const uint32_t combos[][2] = {{1, 1}, {1, 2}, {2, 1}, {2, 2}, {4, 1}};
      constexpr uint32_t N = 4096;
      Dispatch d; d.push = default_push();
      for (const auto& w : sets) for (const auto& c : combos)
        for (uint32_t g = 0; g < N; ++g) {
          FxCase cs = make_case({(float(g) + 0.5f) / float(N), (float((g * 2731u) % N) + 0.5f) / float(N), 0}, kFxExactK);
          cs.c48[0] = w[0]; cs.c48[1] = w[1]; cs.c48[2] = w[2]; cs.rough = w[3]; cs.metal = w[4]; cs.refl = w[5];
          cs.channel = c[0] | (c[1] << 4);
          d.cases.push_back(cs);
        }
      auto r = run(d);
      bool exactOk = true, freqOk = true, meanOk = true, boundOk = true, sameOk = true;
      double worstFreq = 0, worstMean = 0; size_t mismatches = 0, at = 0;
      for (const auto& w : sets) for (const auto& c : combos) {
        const uint32_t K = c[0], NR = c[1];
        std::vector<std::pair<float, uint32_t>> sorted;
        for (uint32_t i = 0; i < 6; ++i) if (w[i] > 0) sorted.push_back({w[i], i});
        std::sort(sorted.begin(), sorted.end(), [](auto a, auto b) { return a.first > b.first; });
        std::vector<uint32_t> exact; double rsum = 0, total = 0;
        for (size_t i = 0; i < sorted.size(); ++i) { total += sorted[i].first; if (i < K) exact.push_back(sorted[i].second); else rsum += sorted[i].first; }
        std::sort(exact.begin(), exact.end());
        uint32_t wantEx = 0;
        for (uint32_t q = 0; q < 4; ++q) wantEx |= (q < exact.size() ? exact[q] : 15u) << (4 * q);
        const size_t nR = sorted.size() - exact.size();
        double freq[6] = {}, mean = 0; size_t groupMismatch = 0;
        for (uint32_t g = 0; g < N; ++g, ++at) {
          const FxOut& o = r[at];
          const FxCase& cs = d.cases[at];
          if (o.sa != wantEx || o.lights != sorted.size()) exactOk = false;
          lk::LkSel s = lk::lk_begin(cs.p[0], cs.p[1]);
          for (uint32_t i = 0; i < 6; ++i) if (w[i] > 0) s = lk::lk_add(s, w[i], i, K);
          s = lk::lk_end(s, NR);
          uint32_t cpuSb = 0;
          for (uint32_t q = 0; q < 2; ++q) cpuSb |= (lk::lk_entry(s, 4 + q) == lk::kLkNone ? 255u : lk::lk_entry(s, 4 + q)) << (8 * q);
          if (cpuSb != o.sb) ++groupMismatch;
          double est = 0;
          for (uint32_t q = 0; q < 4; ++q) { const uint32_t e = (o.sa >> (4 * q)) & 15u; if (e != 15u) est += w[e]; }
          for (uint32_t q = 0; q < NR; ++q) {
            const uint32_t e = (o.sb >> (8 * q)) & 255u;
            if (e == 255u) continue;
            const double f = o.a[q];
            est += w[e] * f;
            if (w[e] * f * NR > rsum * (1 + 1e-6) + 1e-12) boundOk = false;
            if (std::abs(f - rsum / (double(w[e]) * NR)) > 1e-6 * f) boundOk = false;
            if (q == 0) freq[e] += 1.0 / N;
          }
          mean += est / N;
        }
        const double fb = (double(nR) + 1) / N;
        for (uint32_t i = 0; i < 6; ++i) {
          const bool inR = w[i] > 0 && std::find(exact.begin(), exact.end(), i) == exact.end();
          const double dev = std::abs(freq[i] - (inR ? w[i] / rsum : 0.0));
          worstFreq = (std::max)(worstFreq, dev);
          if (dev > fb) freqOk = false;
        }
        const double dm = std::abs(mean - total);
        worstMean = (std::max)(worstMean, dm / total);
        if (dm > fb * rsum + 1e-6 * total) meanOk = false;
        if (groupMismatch > 2 * nR) sameOk = false;
        mismatches += groupMismatch;
      }
      report.line("8a exact-k: exact set = K largest, list order", exactOk, fmt("%.0f selections", double(r.size())));
      report.line("8b exact-k: P(sample j) = w_j / sum_R w", freqOk, fmt("grid %.0f, worst |freq - p| %.2e", N, worstFreq));
      report.line("8c exact-k: mean = full sum, factor <= sum_R w", meanOk && boundOk, fmt("worst rel mean %.2e", worstMean));
      report.line("8d exact-k: GPU selection == C++ header", sameOk, fmt("boundary-cell differences %.0f", double(mismatches)));
    }
    // ---- 9. two-sided frame at a hit (rt_hit_material_instanced, Base_AlphaTest_2s_VT): RtHit.dir is the ray
    //      direction rt_closest_hit traced (bit for bit), and the consumer's N-column sign
    //      nv_two_sided_sign(ng, N, dir) on the GPU equals the C++ one, from both sides of the open floor ----
    {
      Dispatch d; d.push = default_push();
      std::vector<float> want;
      auto ray = [&](float x, float z, D3 dir, D3 n, D3 ng, float t) {
        FxCase c = make_case({cx(RAoOpen) + x, kY, z}, kFxTwoSided);
        const D3 u = unit(dir);
        c.v[0] = float(u.x); c.v[1] = float(u.y); c.v[2] = float(u.z); c.footprint = 0.01f;
        c.n[0] = float(n.x); c.n[1] = float(n.y); c.n[2] = float(n.z);
        c.c48[0] = float(ng.x); c.c48[1] = float(ng.y); c.c48[2] = float(ng.z);
        d.cases.push_back(c); want.push_back(t);
      };
      const D3 up{0, 0, 1}, dn{0, 0, -1}, tilt = unit(D3{0.6, 0, 0.8});
      for (const D3& ng : {up, dn}) {
        ray(-2, 3, dn, up, ng, 3); ray(-1, 3, dn, dn, ng, 3);          // from above: front, back
        ray(1, -3, up, up, ng, 3); ray(2, -3, up, dn, ng, 3);          // from below: back, front
        ray(4, 3, D3{0.6, 0, -0.8}, tilt, ng, 3.75f);                  // oblique, N toward the viewer
        ray(5, 3, D3{0.6, 0, -0.8}, D3{-tilt.x, 0, -tilt.z}, ng, 3.75f);
      }
      auto r = run(d);
      bool signOk = true, dirOk = true, hitOk = true;
      int front = 0, back = 0;
      for (size_t i = 0; i < r.size(); ++i) {
        const FxCase& c = d.cases[i];
        using V = bg3::transport::NvVec3;
        const float ref = bg3::transport::nv_two_sided_sign(V{c.c48[0], c.c48[1], c.c48[2]}, V{c.n[0], c.n[1], c.n[2]}, V{c.v[0], c.v[1], c.v[2]});
        if (r[i].a[0] != ref) { signOk = false; std::printf("  case %zu sign %g (want %g)\n", i, r[i].a[0], ref); }
        (ref > 0 ? front : back) += 1;
        if (std::memcmp(r[i].b, c.v, 12) != 0) { dirOk = false; std::printf("  case %zu RtHit.dir differs from the ray\n", i); }
        if (r[i].sa != 0 || std::abs(r[i].a[1] - want[i]) > 1e-4f) { hitOk = false; std::printf("  case %zu t %g aborted %u\n", i, r[i].a[1], r[i].sa); }
      }
      report.line("9a two-sided N sign == C++ (both sides)", signOk && front > 0 && back > 0, fmt("%.0f rays, %.0f front, %.0f back", double(r.size()), front, back));
      report.line("9b RtHit.dir is the traced ray, floor hit", dirOk && hitOk, fmt("%.0f rays", double(r.size())));
    }
    std::printf("dispatch+readback %.3f s\n", gpuSeconds);
    vkDeviceWaitIdle(dev);
    vkDestroyCommandPool(dev, commandPool, nullptr); vkDestroyPipeline(dev, pipeline, nullptr); vkDestroyPipelineLayout(dev, layout, nullptr);
    vkDestroyDescriptorPool(dev, descriptorPool, nullptr); for (auto l : setLayouts) vkDestroyDescriptorSetLayout(dev, l, nullptr);
    vkDestroySampler(dev, sampler, nullptr); vkDestroyImageView(dev, view, nullptr); vkDestroyImageView(dev, arrayView, nullptr); vkDestroyImageView(dev, slotsView, nullptr);
    vkDestroyImage(dev, texture, nullptr); vkDestroyImage(dev, slots, nullptr); vkFreeMemory(dev, textureMem, nullptr); vkFreeMemory(dev, slotsMem, nullptr);
    destroyAS(dev, tlas, nullptr); destroyAS(dev, blas, nullptr); destroyAS(dev, emTlas, nullptr); destroyAS(dev, emBlas, nullptr);
    vkDestroyImageView(dev, maskView, nullptr); vkDestroyImage(dev, mask, nullptr); vkFreeMemory(dev, maskMem, nullptr);
  }
  vkDestroyDevice(dev, nullptr);
  destroyDebug(instance, messenger, nullptr);
  vkDestroyInstance(instance, nullptr);
  const unsigned errors = validationErrors.load();
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::printf("%d groups, %d failed, validation errors %u, wall %.3f s\n", report.groups, report.failures, errors, seconds);
  return report.failures == 0 && errors == 0 ? 0 : 1;
}
