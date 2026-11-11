#pragma once
#include "settings.h"

namespace bg3::experimental_ab {
// The all-ray-tracing hotkey (hotkey_rt_toggle) always selects the master switch, independent of the saved target.
enum class HotkeyAction { None, Master, Experimental };
inline HotkeyAction hotkey_action(bool master_edge, bool target_edge, bool enabled) {
  return !enabled ? HotkeyAction::None : master_edge ? HotkeyAction::Master :
         target_edge ? HotkeyAction::Experimental : HotkeyAction::None;
}
// Only normal Experimental choices with live transitions can become a target.
inline bool eligible(const settings::Knob& k, int c, bool opt_in = false) {
  return k.experimental && k.kind == settings::Kind::Choice &&
         c >= 0 && c < settings::choice_count(k) && settings::choice_visible(k,c,false,opt_in) &&
         k.choices[c].apply != settings::Apply::Restart && k.choices[c].apply != settings::Apply::Unspecified;
}
struct Selection {
  std::string target;
  int on = 1;
  bool pending = false;
  void touch(const settings::Knob& k, int c, bool opt_in = false) {
    if (!eligible(k,c,opt_in) || !eligible(k,0,opt_in)) return;
    int next = c > 0 ? c : 1;
    if (!eligible(k,next,opt_in)) return;
    target = k.id; on = next; pending = true;
  }
  void append(settings::KVList& keys) const {
    if (!pending) return;
    keys.emplace_back("ab_target",target);
    keys.emplace_back("ab_choice",std::to_string(on));
  }
};
// Mutate the current effective map, so successive flips see the preceding flip.
// File reload replaces this map and therefore supersedes all hotkey changes.
inline bool flip(settings::Map& values, std::string& indicator) {
  int i = settings::knob_index(settings::lookup(values,"ab_target"));
  if (i < 0) return false;
  const auto& k = settings::kKnobs[i];
  bool opt_in = settings::map_int(values,"experimental_options",0) == 1;
  int on = std::atoi(settings::lookup(values,"ab_choice").c_str());
  if (!eligible(k,0,opt_in) || on <= 0 || !eligible(k,on,opt_in)) return false;
  int current = settings::knob_value(k,values);
  if (current < 0) return false;
  int next = current == 0 ? on : 0;
  settings::KVList keys; settings::knob_kvs(k,next,keys);
  for (auto& kv : keys) values[kv.first] = kv.second;
  indicator = "A/B:";
  for (auto& kv : keys) indicator += " " + kv.first + "=" + kv.second;
  return true;
}
}
