#pragma once
#include "omm_classify.h"
#include <cmath>
#include <cstdint>

namespace bg3::omm {

// Generations are supplied by the eventual GPU provider, not raw Vulkan handles
// or CPU frame numbers. Zero means the resource/content proof is unavailable.
struct ImageRevision {
  uint64_t image = 0, view = 0, content = 0;
  bool operator==(const ImageRevision&) const = default;
  bool valid() const { return image && view && content; }
};

enum class AlphaSource { Unsupported, Plain, VirtualTexture };
struct BakeIdentity {
  uint64_t device = 0, descriptor_epoch = 0;
  uint64_t topology = 0, uv = 0, material = 0;
  uint64_t sampler = 0, sample_precision = 0, evaluator = 0;
  ImageRevision texture, cache;
  uint64_t residency = 0;
  AlphaSource source = AlphaSource::Unsupported;
  uint32_t view_format = 0, channel = 0, policy_flags = 0;
  uint32_t mip_count = 0, first_mip = 0, last_mip = 0;
  uint32_t primitive = 0, microtriangle = 0, subdivision = 0;
  float scale_x = 1, scale_y = 1, first_cut = 0.5f, effective_cut = 0.5f;
  bool operator==(const BakeIdentity&) const = default;

  bool valid() const {
    if (!device || !descriptor_epoch || !topology || !uv || !material ||
        !sampler || !sample_precision || !evaluator || !texture.valid() ||
        !view_format || channel > 3 || !mip_count || first_mip > last_mip || last_mip >= mip_count ||
        subdivision > 15 || uint64_t(microtriangle) >= (uint64_t(1) << (2 * subdivision)))
      return false;
    if (!std::isfinite(scale_x) || !std::isfinite(scale_y) ||
        !std::isfinite(first_cut) || !std::isfinite(effective_cut) ||
        first_cut < 0 || first_cut > 1 || effective_cut < 0 || effective_cut > 1)
      return false;
    // The subdivision bound is this CPU representation's 32-bit index limit,
    // not a claim about device support (queried by the future backend).
    return source == AlphaSource::Plain ||
           (source == AlphaSource::VirtualTexture && cache.valid() && residency);
  }
};

// The ticket must come from successful SUBMISSION of the producing work, and
// the observation from that exact completion timeline. Recording/age is not
// completion; a recycled fence handle is not the same timeline generation.
struct BakeTicket {
  uint64_t submission = 0, timeline = 0, value = 0;
};
struct CompletionObservation {
  uint64_t timeline = 0, value = 0;
};
inline bool completed(BakeTicket ticket, CompletionObservation observed) {
  return ticket.submission && ticket.timeline && ticket.value &&
         observed.timeline == ticket.timeline && observed.value >= ticket.value;
}

struct BakedState {
  BakeIdentity identity;
  BakeTicket ticket;
  State state = State::Unknown;
  bool sampling_domain_proved = false;
};

// Fail closed. A code hull alone is not sampling_domain_proved: the provider
// must also certify filtering, numeric conversion, UV/mip support and residency.
// These CPU primitives do not create such a GPU certificate or attach OMMs.
inline State reusable_state(const BakedState& saved, const BakeIdentity& current,
                            CompletionObservation observed) {
  if ((saved.state != State::Transparent && saved.state != State::Opaque) ||
      !saved.sampling_domain_proved || !saved.identity.valid() || !current.valid() ||
      saved.identity != current || !completed(saved.ticket, observed))
    return State::Unknown;
  return saved.state;
}
} // namespace bg3::omm
