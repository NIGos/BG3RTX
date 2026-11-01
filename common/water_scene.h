#pragma once
// M5.2 water reflections (docs/PLAN_M5_WATER.md, rt_water_reflections): the water scene the
// layer (layer/water.cpp, rt_water_geometry) hands to the core WITHOUT an ABI change, through
// host persist memory under kSceneKey, the BG3_DEVICE_INFO_KEY pattern (common/host_api.h).
// The ABI 25 water scene of the plan (records on the GPU, the set of a consumer) replaces it
// once the replay branch's ABI bump is merged; until then the core needs only the TLAS, the
// build's camera and the roles of the dominant draw.
//
// Publication is a sequence lock: the layer writes after the build submission (its submit
// thread), the core reads while it records the lighting (another thread). seq is odd while a
// write is in progress; a reader that sees an odd seq, or a different seq after its copy,
// skips the frame (counted). Pure: no Vulkan calls (the handle is a uint64_t).
//
// Lifetime: the TLAS is one of layer/water.cpp's kOwn slots. A slot is rebuilt only kOwn
// builds later, in a command buffer of the layer that is submitted after the game's batch on
// the same queue and starts with an ALL_COMMANDS barrier (its first scope holds every earlier
// submission, so the consumer's reads of frame N finish before the rebuild at N + kOwn - 1);
// grown storage goes through defer_destroy. The core uses a scene at most kMaxAge frames old.
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace bg3::water {

inline constexpr const char* kSceneKey = "bg3rtx.water_scene";
inline constexpr uint32_t kSceneVersion = 1;
inline constexpr uint64_t kMaxAge = 2;   // frames between the build's submission and its use

struct Scene {
  uint32_t seq;            // sequence lock: odd while the layer writes
  uint32_t version;        // kSceneVersion (0: never published)
  uint64_t tlas;           // VkAccelerationStructureKHR of the build (one instance, identity)
  uint64_t built_frame;    // the layer's frame_index() at the build's submission
  float cam[3];            // absolute camera position of the build's pass (set 0 b2 byte 208)
  uint32_t cam_ok;         // cam was read from the pass's mapped camera UBO and is finite
  float reflectance;       // Reflectance_Base of the first supported draw (record roles)
  float roughness;         // Roughness_Base of it
  uint32_t family;         // its kAudited index (kNoFamily-like UINT32_MAX when none)
  uint32_t draws;          // geometries (captured draws) in the build
  uint64_t triangles;      // triangles in the build
  uint32_t pad[4];
};
static_assert(sizeof(Scene) == 80, "Scene layout is shared by the layer and the core");

// Layer side: one complete publication.
inline void publish(Scene* dst, const Scene& src) {
  std::atomic_ref<uint32_t> seq(dst->seq);
  const uint32_t s = seq.load(std::memory_order_relaxed);
  seq.store(s | 1u, std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_release);
  Scene body = src;
  body.seq = s | 1u;
  body.version = kSceneVersion;
  // Everything but seq (the first word).
  std::memcpy(reinterpret_cast<uint8_t*>(dst) + 4, reinterpret_cast<const uint8_t*>(&body) + 4, sizeof(Scene) - 4);
  std::atomic_thread_fence(std::memory_order_release);
  seq.store((s | 1u) + 1u, std::memory_order_release);
}

// Core side: a consistent copy, or false (a write in progress or torn: try next frame).
inline bool read(const Scene* src, Scene& out) {
  Scene* s = const_cast<Scene*>(src);
  std::atomic_ref<uint32_t> seq(s->seq);
  const uint32_t a = seq.load(std::memory_order_acquire);
  if (a & 1u) return false;
  std::memcpy(&out, src, sizeof(Scene));
  std::atomic_thread_fence(std::memory_order_acquire);
  const uint32_t b = seq.load(std::memory_order_relaxed);
  out.seq = a;
  return a == b;
}

enum Usable : uint32_t {
  kUsable = 0,
  kNoScene = 1,     // never published (rt_water_geometry off since the start, or an old layer)
  kStale = 2,       // older than kMaxAge (no water build lately: no water in view, or the key off)
  kTorn = 3,        // the read raced a publication
  kNoTlas = 4,      // published without an acceleration structure
  kUsableCount = 5
};

// Whether the core may trace a scene it read (ok: the result of read()) in frame `now`.
inline Usable usable(bool ok, const Scene& s, uint64_t now) {
  if (!ok) return kTorn;
  if (s.version != kSceneVersion) return kNoScene;
  if (!s.tlas) return kNoTlas;
  if (s.built_frame > now || now - s.built_frame > kMaxAge) return kStale;
  return kUsable;
}

// The shift that moves a point of the current lighting space (camera-relative, camera now
// at cam_now) into the build's camera-relative space: p_build = p_now + shift. Zero when a
// camera is unknown or the difference is not finite (static-camera exactness either way).
inline void shift(const Scene& s, const float cam_now[3], bool now_ok, float out[3]) {
  out[0] = out[1] = out[2] = 0.0f;
  if (!s.cam_ok || !now_ok) return;
  float d[3];
  for (int i = 0; i < 3; ++i) {
    d[i] = cam_now[i] - s.cam[i];
    if (!std::isfinite(d[i]) || std::fabs(d[i]) > 1e6f) return;
  }
  for (int i = 0; i < 3; ++i) out[i] = d[i];
}

// F0 of the water surface in the game's convention (FRAME_MAP RT2.y, the water FS's _1470):
// 0.16 * reflectance^2. Reflectance_Base 0.35 (S1 docks) gives 0.0196, water's IOR 1.33
// gives 0.0200. Non-finite or out-of-range roles fall back to that of IOR 1.33.
inline float f0_of(float reflectance) {
  if (!std::isfinite(reflectance) || reflectance < 0.0f || reflectance > 1.0f) return 0.02f;
  return 0.16f * reflectance * reflectance;
}

}  // namespace bg3::water
