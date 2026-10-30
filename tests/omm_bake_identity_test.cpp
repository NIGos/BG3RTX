#include "../common/omm_bake_identity.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>

using namespace bg3::omm;
unsigned checks = 0;
void check(bool ok, const char* why) {
  ++checks;
  if (!ok) { std::fprintf(stderr, "FAIL %s\n", why); std::exit(1); }
}
BakeIdentity plain() {
  BakeIdentity k;
  k.device = 1; k.descriptor_epoch = 2; k.topology = 3; k.uv = 4; k.material = 5;
  k.sampler = 6; k.sample_precision = 7; k.evaluator = 8;
  k.texture = {9, 10, 11}; k.source = AlphaSource::Plain;
  k.view_format = 37; k.channel = 3; k.mip_count = 3; k.last_mip = 2;
  return k;
}
int main() {
  auto key = plain();
  BakedState record{key, {1, 42, 9}, State::Opaque, true};
  CompletionObservation ready{42, 9};
  auto read = [&](const BakeIdentity& k) { return reusable_state(record, k, ready); };
  check(read(key) == State::Opaque, "same certified completed bake can be reused");
  check(reusable_state(record, key, {42, 8}) == State::Unknown, "in-flight bake stays unknown");
  check(reusable_state(record, key, {43, 99}) == State::Unknown, "another timeline cannot certify completion");
  check(!completed({0, 42, 9}, ready), "recorded but unsubmitted work is unavailable");
  check(!completed({1, 0, 9}, ready), "unknown timeline");
  check(!completed({1, 42, 0}, ready), "unpublished completion target");
  record.sampling_domain_proved = false;
  check(read(key) == State::Unknown, "raw alpha code hull is not a sampling proof");
  record.sampling_domain_proved = true;
  record.state = State::Unknown; check(read(key) == State::Unknown, "unknown is never promoted");
  record.state = State::Transparent; check(read(key) == State::Transparent, "transparent known state follows same guards");
  record.state = State::Opaque;

  // Exact identity comparisons, not a hash collision or frame-age heuristic.
  for (auto member : {&BakeIdentity::device, &BakeIdentity::descriptor_epoch, &BakeIdentity::topology,
                     &BakeIdentity::uv, &BakeIdentity::material, &BakeIdentity::sampler,
                     &BakeIdentity::sample_precision, &BakeIdentity::evaluator}) {
    auto k = key; ++(k.*member); check(read(k) == State::Unknown, "generation change invalidates");
    k = key; k.*member = 0; check(read(k) == State::Unknown, "missing proof generation invalidates");
  }
  for (auto member : {&ImageRevision::image, &ImageRevision::view, &ImageRevision::content}) {
    auto k = key; ++(k.texture.*member); check(read(k) == State::Unknown, "in-place texture/view/content change invalidates");
    k = key; k.texture.*member = 0; check(read(k) == State::Unknown, "unproven texture revision invalidates");
  }
  for (auto member : {&BakeIdentity::view_format, &BakeIdentity::channel, &BakeIdentity::policy_flags,
                     &BakeIdentity::mip_count, &BakeIdentity::first_mip, &BakeIdentity::last_mip,
                     &BakeIdentity::primitive, &BakeIdentity::microtriangle, &BakeIdentity::subdivision}) {
    auto k = key; ++(k.*member); check(read(k) == State::Unknown, "sampling identity change invalidates");
  }
  for (auto member : {&BakeIdentity::scale_x, &BakeIdentity::scale_y, &BakeIdentity::first_cut, &BakeIdentity::effective_cut}) {
    auto k = key; k.*member += 0.125f; check(read(k) == State::Unknown, "scale/cut policy change invalidates");
    k = key; k.*member = std::numeric_limits<float>::quiet_NaN(); check(read(k) == State::Unknown, "nonfinite domain invalidates");
  }
  auto invalid = key; invalid.channel = 4; check(!invalid.valid(), "invalid alpha channel");
  invalid = key; invalid.last_mip = 3; check(!invalid.valid(), "mip beyond supplied domain");
  invalid = key; invalid.first_mip = 2; invalid.last_mip = 1; check(!invalid.valid(), "inverted mip domain");
  invalid = key; invalid.first_cut = -0.01f; check(!invalid.valid(), "unsupported negative threshold");
  invalid = key; invalid.effective_cut = 1.01f; check(!invalid.valid(), "unsupported high threshold");
  invalid = key; invalid.source = AlphaSource::Unsupported; check(!invalid.valid(), "unsupported source");
  invalid = key; invalid.microtriangle = 1; check(!invalid.valid(), "microtriangle outside subdivision");
  invalid = key; invalid.subdivision = 16; check(!invalid.valid(), "subdivision outside CPU representation");
  invalid = key; invalid.subdivision = 1; invalid.microtriangle = 3;
  check(invalid.valid(), "last valid microtriangle in subdivision");

  auto vt = key; vt.source = AlphaSource::VirtualTexture;
  check(!vt.valid(), "VT without page-cache/residency proof is unknown");
  vt.cache = {12, 13, 14}; vt.residency = 15;
  check(vt.valid(), "complete VT identity is representable, not a GPU proof");
  record.identity = vt;
  check(reusable_state(record, vt, ready) == State::Opaque, "certified VT identity follows guards");
  for (auto member : {&ImageRevision::image, &ImageRevision::view, &ImageRevision::content}) {
    auto k = vt; ++(k.cache.*member);
    check(reusable_state(record, k, ready) == State::Unknown, "same live cache handle with changed pages is invalidated");
  }
  auto k = vt; ++k.residency;
  check(reusable_state(record, k, ready) == State::Unknown, "VT residency update invalidates");
  k = vt; k.residency = 0; check(!k.valid(), "missing VT residency cannot be certified");
  std::printf("PASS %u bake identity/completion checks; no GPU or runtime changes\n", checks);
}
