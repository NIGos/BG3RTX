#pragma once
// Native Sun-mask receiver draw (FS 0687fa327aed7f8e): which bindings and UBO
// byte ranges the layer copies at the draw. Data only; layer/sun_scene.cpp
// sun_note_native_draw walks this table. tests/native_sun_packets_test.cpp
// pins every value: a game patch that moves them must change both.
#include <cstdint>
namespace bg3::sun_packets {
inline constexpr uint64_t kReceiverFs = 0x0687fa327aed7f8eull;
struct UboPacket { uint32_t set, binding, offset, size; };
struct ImageBinding { uint32_t set, binding; };
// Order matches SunReceiverCapture: frame, camera, sun, auxiliary, ray_basis.
inline constexpr UboPacket kFrame{0, 0, 0, 32};       // native frame epoch words
inline constexpr UboPacket kCamera{0, 2, 0, 224};     // camera constants
inline constexpr UboPacket kSun{2, 26, 0, 544};       // sun/cascade constants
inline constexpr UboPacket kAuxiliary{1, 0, 0, 144};  // auxiliary shadow enable/selection
inline constexpr UboPacket kRayBasis{0, 1, 112, 48};  // ray basis
inline constexpr ImageBinding kCsm{2, 8};             // cascade shadow map (required)
inline constexpr ImageBinding kAuxiliaryNear{1, 3};   // optional, not evidence of enable
inline constexpr ImageBinding kAuxiliaryFar{1, 4};
inline constexpr uint32_t kMaxPacketBytes = 544;      // SunReceiverPacket::bytes capacity
inline constexpr uint32_t kEpochBytes = 32;           // bytes of kFrame copied into the epoch
} // namespace bg3::sun_packets
