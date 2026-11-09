// Background-run input filter (layer/bg_input.h): runner mouse messages pass, real ones are
// dropped only while another window is foreground, keys are untouched; and every mouse
// message scripts/auto-run.ps1 posts carries the mark.
#include "../layer/bg_input.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
using namespace bg3::bginput;
static int fails = 0;
static void check(bool b, const char* what) {
  if (!b) { std::fprintf(stderr, "bg_input check failed: %s\n", what); ++fails; }
}
int main(int argc, char** argv) {
  constexpr uint32_t MOVE = 0x200, LDOWN = 0x201, MDOWN = 0x207, MUP = 0x208, WHEEL = 0x20A, HWHEEL = 0x20E;
  constexpr uint32_t KEYDOWN = 0x100, KEYUP = 0x101, INPUT = 0xFF, LEAVE = 0x2A3, HOVER = 0x2A1, NCMOVE = 0xA0;
  // Filter idle (game in front, or normal play): everything passes; a stray mark is still stripped.
  for (uint32_t m : {MOVE, LDOWN, MDOWN, WHEEL, LEAVE, KEYDOWN, INPUT}) check(classify(m, 0, false) == Verdict::Pass, "idle passes");
  check(classify(MOVE, kPostedMark, false) == Verdict::PassStrip, "idle strips mark");
  // Filter active: real mouse and leave/hover dropped.
  for (uint32_t m : {MOVE, LDOWN, MDOWN, MUP, WHEEL, HWHEEL}) check(classify(m, 0x10, true) == Verdict::Drop, "real mouse dropped");
  check(classify(LEAVE, 0, true) == Verdict::Drop, "leave dropped");
  check(classify(HOVER, 0, true) == Verdict::Drop, "hover dropped");
  // Runner messages pass, including a wheel with a negative delta in the high word.
  check(classify(MOVE, kPostedMark, true) == Verdict::PassStrip, "posted move");
  check(classify(MDOWN, 0x10 | kPostedMark, true) == Verdict::PassStrip, "posted middle down");
  check(classify(WHEEL, (uint64_t(0xFF88) << 16) | kPostedMark, true) == Verdict::PassStrip, "posted wheel -120");
  check(classify(WHEEL, 0xFFFFFFFFFF880000ull | kPostedMark, true) == Verdict::PassStrip, "posted wheel, sign-extended");
  check(classify(WHEEL, 0xFFFFFFFFFF880000ull, true) == Verdict::Drop, "real wheel -120");
  // Keys, raw input and non-client messages are never touched.
  for (uint32_t m : {KEYDOWN, KEYUP, INPUT, NCMOVE}) check(classify(m, 0x44, true) == Verdict::Pass, "non-mouse passes");
  check(classify(KEYDOWN, kPostedMark, true) == Verdict::Pass, "key with mark bit untouched");
  // No MK_* flag overlaps the mark.
  check((kPostedMark & 0x7F) == 0, "mark disjoint from MK flags");
  check(sdl_buttons(0x10 | kPostedMark) == 2, "middle -> SDL mask 2");
  check(sdl_buttons(0x01) == 1 && sdl_buttons(0x02) == 4 && sdl_buttons(0x60) == 24, "button masks");

  // Every PostMessage of a mouse message (0x0200-0x020E) in auto-run.ps1 carries $BgMouseMark,
  // and the runner's constant equals kPostedMark.
  if (argc > 1) {
    std::ifstream f(std::string(argv[1]) + "/scripts/auto-run.ps1");
    check(f.good(), "auto-run.ps1 readable");
    std::stringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str(), line;
    std::smatch mm;
    check(std::regex_search(s, mm, std::regex(R"(\$BgMouseMark = 0x([0-9A-Fa-f]+))")) &&
              std::stoull(mm[1].str(), nullptr, 16) == kPostedMark, "runner mark equals kPostedMark");
    std::istringstream in(s);
    int posted = 0;
    std::regex mouse_literal(R"(PostMessage\(\$h, 0x020[0-9A-Ea-e],)");
    std::regex generic(R"(PostMessage\(\$h, \[uint32\]\$message,)");
    while (std::getline(in, line)) {
      if (!std::regex_search(line, mouse_literal) && !std::regex_search(line, generic)) continue;
      ++posted;
      if (line.find("$BgMouseMark") == std::string::npos) {
        std::fprintf(stderr, "unmarked posted mouse message: %s\n", line.c_str());
        ++fails;
      }
    }
    check(posted >= 9, "found the runner's posted mouse messages");
  }
  if (fails) return 1;
  std::printf("bg_input_test: ok\n");
  return 0;
}
