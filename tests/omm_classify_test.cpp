#include "../common/omm_classify.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>

using namespace bg3::omm;
static unsigned checks = 0;
static void check(bool ok, const char* why) {
  ++checks;
  if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}

int main() {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  const auto all = legacy_thresholds(0.5f, true);
  check(classify({0.75f, 0.75f, true}, all) == State::Unknown, "fractional opaque at fine cut is not universally opaque");
  check(classify({0.25f, 0.25f, true}, all) == State::Unknown, "fractional transparent at fine cut is not universally transparent");
  check(classify({0, 0, true}, all) == State::Unknown, "zero alpha hits at hash zero under legacy >= semantics");
  check(classify({1, 1, true}, all) == State::Opaque, "unit alpha covers every supported threshold");
  check(classify({0, 0.49f, true}, legacy_thresholds(.5f, false)) == State::Transparent, "hard-cut domain can prove transparency");
  check(classify({.5f, 1, true}, legacy_thresholds(.5f, false)) == State::Opaque, "inclusive hard cutoff");
  check(classify({0, .5f, true}, legacy_thresholds(.5f, false)) == State::Unknown, "strict transparent boundary");
  for (auto invalid : {Interval{0, 1, false}, Interval{1, 0, true}, Interval{nan, 1, true}, Interval{0, inf, true}}) {
    check(classify(invalid, all) == State::Unknown, "incomplete/invalid sampling support must fall back");
    check(classify({1, 1, true}, invalid) == State::Unknown, "invalid threshold proof must fall back");
  }
  check(!legacy_thresholds(-.1f, true).complete, "unsupported cut below zero");
  check(!legacy_thresholds(1.1f, true).complete, "unsupported cut above one");
  check(!legacy_thresholds(nan, false).complete, "nonfinite cut");

  // Independent oracle: evaluate the runtime predicate, not the interval
  // inequalities. Include both HLSL lerp lowering forms, hash endpoints, hard
  // and transitional footprints, every alpha sample in each test interval.
  const float hashes[] = {0, 1.f/16777216.f, .125f, .25f, .5f, .75f, .875f, 16777215.f/16777216.f};
  const float ks[] = {0, .125f, .5f, .875f, 1};
  unsigned known = 0, opaque = 0, transparent = 0;
  for (bool stochastic : {false, true}) for (float cut : {0.f, .25f, .5f, 1.f})
    for (int lo = 0; lo <= 8; ++lo) for (int hi = lo; hi <= 8; ++hi) {
      const auto state = classify({lo/8.f, hi/8.f, true}, legacy_thresholds(cut, stochastic));
      if (state == State::Unknown) continue;
      ++known; opaque += state == State::Opaque; transparent += state == State::Transparent;
      for (int ai = lo*8; ai <= hi*8; ++ai) for (float k0 : ks) for (float hash : hashes) {
        const float alpha = ai/64.f, k = stochastic ? k0 : 0;
        const float ordinary = k > 0 ? cut + k * (hash-cut) : cut;
        const float fused = k > 0 ? std::fma(k, hash-cut, cut) : cut;
        const bool wanted = state == State::Opaque;
        check((alpha >= ordinary) == wanted, "known state contradicts nonfused runtime predicate");
        check((alpha >= fused) == wanted, "known state contradicts fused runtime predicate");
      }
    }
  check(known > 0 && opaque > 0 && transparent > 0, "oracle exercised both known outcomes");
  // Counterexample to the old two-mip proposal: a constant alpha texture keeps
  // the same alpha at EVERY mip, yet coarse stochastic rays disagree with the
  // hard cutoff on a nonzero fraction of the hash grid.
  for (float alpha : {.25f, .75f}) {
    unsigned hit = 0;
    for (unsigned h = 0; h < 1024; ++h) hit += alpha >= h/1024.f;
    check(hit > 0 && hit < 1024, "constant fractional texture has mixed coarse visibility");
  }
  std::printf("PASS: %u checks, %u known interval cases (%u opaque, %u transparent); no GPU/runtime changes\n",
              checks, known, opaque, transparent);
}
