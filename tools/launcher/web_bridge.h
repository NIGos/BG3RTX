#pragma once
#include "launcher.h"
#include "version.h"
#include <functional>
#include <map>
#include "../../common/experimental_ab.h"

namespace launcher::web {
// The host supplies side effects. Tests inject CPU-only callbacks; JS never
// derives settings policy or passes file paths to the collector/exporter.
struct Hooks {
  // Writes the candidate to the user file. Called by every change (instant apply); false leaves the state as it was.
  std::function<bool(State&, const bg3::settings::KVList&, std::string&)> save;
  std::function<bool(std::string&)> play;
  std::function<diagnostics::Snapshot(const std::string&, bool, bool, bool, bool)> prepareReport; // issue, logs, settings, system, latest save
  std::function<bool(const diagnostics::Snapshot&, std::string&)> exportReport;
  std::function<std::string()> browseGame;
  std::function<std::string()> browseDlss5Runtime;
  std::function<std::string(const std::string&)> dlss5RuntimeStatus;
  std::function<GraphicsInspection()> recheck;
  std::function<void()> notices;
  std::function<std::string()> reportHint;
  // Preparation (tools/launcher/preparation.h): its JSON object, and its actions (pause, resume, convertAll,
  // whilePlaying). Launcher-only settings: never part of the game's settings.
  std::function<std::string()> preparation;
  std::function<bool(const std::string&, int)> prepare;
};
class Bridge {
 public:
  Bridge(State&, Hooks = {});
  std::string handle(const std::string& message);
  std::string state_json() const;
  static bool is_collection_intent(const std::string& message);
  static bool is_export_intent(const std::string& message);
  static bool is_cancel_collection_intent(const std::string& message);
  void set_compatibility(GraphicsInspection inspection, int selected = -1);
  void set_can_play(bool value) { can_play_ = value; }
  void set_status(std::string text) { status_ = std::move(text); }
  // Settings that wait for a game restart, by name (the host compares the file with what the running game started
  // with). `others` counts changed startup keys that belong to no visible setting.
  void set_restart(std::vector<std::string> names, int others = 0) { restart_names_ = std::move(names); restart_others_ = std::max(0,others); }
  void set_game_path(std::string value) { game_path_ = std::move(value); }
  void set_mod_status(std::string value) { mod_status_ = std::move(value); }
  void set_version(std::string value) { version_ = std::move(value); }
  // dev=1: the developer Experimental disclosure. Release: the "Show experimental options" opt-in.
  bool dev = false, experimental = false;
 private:
  // What Undo restores: everything a click can change.
  struct Undo { std::vector<int> values; int preset = 0; std::array<std::string, kShortcutCount> shortcuts; std::string dlss5_runtime_path; bool experimental = false; };
  // A write that failed, kept as it was for Retry.
  struct Pending { State next; bool experimental = false; bg3::settings::KVList keys; std::string headline, anchor; bool undoable = true; };
  bool opt_in() const { return !dev && experimental; }
  bool shown(const bg3::settings::Knob& k) const;
  bg3::experimental_ab::Selection ab_selection_;
  bg3::settings::Map effective() const;
  bg3::settings::Map effective(const State& s) const;
  bool resolve(State& next, const std::vector<int>& before, bg3::settings::KVList& keys, std::string& blocked) const;
  bool commit(State next, bool next_experimental, bg3::settings::KVList keys, const std::string& headline, const std::string& anchor, bool undoable = true);
  State& state_;
  Hooks hooks_;
  Undo undo_;
  Pending retry_;
  bool can_undo_ = false, has_retry_ = false;
  std::map<int, int> remembered_;   // dependents lowered with their prerequisite: knob index -> the choice to restore
  std::string notice_, notice_anchor_, write_error_;
  std::vector<std::string> notice_also_, restart_names_;
  int restart_others_ = 0;
  std::string status_, game_path_, report_hint_, mod_status_ = "Not checked", version_ = display_version("");
  bool can_play_ = false, report_visible_ = false;
  GraphicsInspection inspection_;
  Compatibility compatibility_;
  int selected_ = -1;
  diagnostics::Snapshot report_;
};
}
