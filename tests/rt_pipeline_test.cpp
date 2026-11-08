// common/rt_pipeline.h: device feature decision, SBT layout / alignment, the compute fallback decision.
#include "../common/rt_pipeline.h"
#include <cstdio>
#include <initializer_list>

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                         \
  do {                                                                   \
    ++g_checks;                                                          \
    if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } \
  } while (0)

int main() {
  using namespace bg3::rt_pipeline;
  // Feature decision: both extensions and features are needed; the application's choice is never overridden.
  DeviceQuery q;
  q.pipeline_extension = q.reorder_extension = q.pipeline_feature = q.reorder_feature = true;
  FeatureDecision d = decide_features(q);
  CHECK(d.result == Feature::kRequest && d.add_pipeline_struct && d.add_reorder_struct);
  for (int miss = 0; miss < 4; ++miss) {
    DeviceQuery m = q;
    (miss == 0 ? m.pipeline_extension : miss == 1 ? m.reorder_extension : miss == 2 ? m.pipeline_feature : m.reorder_feature) = false;
    CHECK(decide_features(m).result == Feature::kUnsupported);
    CHECK(!decide_features(m).add_pipeline_struct && !decide_features(m).add_reorder_struct);
  }
  DeviceQuery app = q;
  app.app_pipeline_struct = true; app.app_pipeline_value = true;   // the app (or another layer) already enables it
  d = decide_features(app);
  CHECK(d.result == Feature::kRequest && !d.add_pipeline_struct && d.add_reorder_struct);
  app.app_pipeline_value = false;                                   // ... or chains it switched off
  CHECK(decide_features(app).result == Feature::kAppDisabled);
  app = q; app.app_reorder_struct = true; app.app_reorder_value = false;
  CHECK(decide_features(app).result == Feature::kAppDisabled);

  // SBT: RTX 5090 values (handle 32, handle alignment 32, base alignment 64) and other legal ones.
  SbtLayout l = sbt_layout(32, 32, 64);
  CHECK(l.ok && l.stride == 32 && l.slot_bytes == 64);
  l = sbt_layout(32, 64, 64);
  CHECK(l.ok && l.stride == 64 && l.slot_bytes == 64);
  l = sbt_layout(48, 16, 256);
  CHECK(l.ok && l.stride == 48 && l.slot_bytes == 256);
  CHECK(!sbt_layout(0, 32, 64).ok && !sbt_layout(32, 24, 64).ok && !sbt_layout(32, 32, 96).ok && !sbt_layout(32, 32, 0).ok);
  l = sbt_layout(32, 32, 64);
  CHECK(sbt_buffer_bytes(l, 64, 64) == 64 * 64 + 64);
  // Every slot address is base-aligned and inside the buffer, whatever the buffer's own alignment.
  for (uint64_t address : {uint64_t(0x10000), uint64_t(0x10010), uint64_t(0x1003f), uint64_t(0x10040)})
    for (uint32_t s = 0; s < 64; ++s) {
      const uint64_t a = sbt_slot_address(address, l, s, 64);
      CHECK(a % 64 == 0 && a >= address && a + l.stride <= address + sbt_buffer_bytes(l, 64, 64));
    }
  CHECK(sbt_slot_address(0x10010, l, 1, 64) - sbt_slot_address(0x10010, l, 0, 64) == 64);

  // The dispatch path: every missing piece falls back to compute, in this order.
  CHECK(choose_path(true, true, true, true, true) == Path::kRaygen);
  CHECK(choose_path(false, true, true, true, true) == Path::kKeyOff);
  CHECK(choose_path(true, false, true, true, true) == Path::kNoDevice);
  CHECK(choose_path(true, true, false, true, true) == Path::kNoEntryPoints);
  CHECK(choose_path(true, true, true, false, true) == Path::kVariant);
  CHECK(choose_path(true, true, true, true, false) == Path::kNotReady);
  CHECK(choose_path(false, false, false, false, false) == Path::kKeyOff);
  for (uint32_t p = 0; p <= 5; ++p) CHECK(path_name(Path(p))[0] != '?');
  // The reorder key: 1 = the hit-kind hint (shader value 2), 2 = kind + instance (1), else none.
  CHECK(reorder_spec(0) == 0 && reorder_spec(1) == 2 && reorder_spec(2) == 1 && reorder_spec(7) == 0 && reorder_spec(-1) == 0);
  // Device fallback: a driver without reordering (hint mode NONE) gets no hint and no prepass for every key.
  for (int key = -1; key <= 3; ++key) CHECK(reorder_spec(key, false) == 0 && reorder_spec(key, true) == reorder_spec(key));
  // No reorder extension: decide_features never requests the pipeline (the raygen module declares the capability).
  DeviceQuery no_reorder = q; no_reorder.reorder_extension = false;
  CHECK(decide_features(no_reorder).result == Feature::kUnsupported);

  std::printf("rt_pipeline_test: %d checks, %d failures\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
