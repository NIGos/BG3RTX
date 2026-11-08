#pragma once
#include "launcher.h"
#include <functional>
#include "../../common/experimental_ab.h"

namespace launcher::web {
// The host supplies side effects. Tests inject CPU-only callbacks; JS never
// derives settings policy or passes file paths to the collector/exporter.
struct Hooks {
  std::function<bool(State&, const bg3::settings::KVList&, std::string&)> save;
  std::function<bool(std::string&)> play;
  std::function<diagnostics::Snapshot(const std::string&, bool, bool, bool)> prepareReport;
  std::function<bool(const diagnostics::Snapshot&, std::string&)> exportReport;
  std::function<std::string()> browseGame;
  std::function<std::string()> browseDlss5Runtime;
  std::function<std::string(const std::string&)> dlss5RuntimeStatus;
  std::function<GraphicsInspection()> recheck;
  std::function<void()> notices;
  std::function<std::string()> reportHint;
  // Preparation card (tools/launcher/preparation.h): its JSON object, and its actions (pause, resume, convertAll,
  // whilePlaying). Launcher-only settings: never part of the game's settings or the unsaved-change count.
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
  void set_restart_count(int value) { restart_count_ = std::max(0,value); }
  void set_game_path(std::string value) { game_path_ = std::move(value); }
  void set_mod_status(std::string value) { mod_status_ = std::move(value); }
  int dirty() const;
  // dev=1: the developer Experimental disclosure. Release: the "Show experimental options" opt-in.
  bool dev = false, experimental = false;
 private:
  bool opt_in() const { return !dev && experimental; }
  bool shown(const bg3::settings::Knob& k) const;
  bool saved_experimental_ = false;
  bg3::experimental_ab::Selection ab_selection_;
  bg3::settings::Map effective() const;
  bool save_preview(bool enable_required);
  State& state_;
  Hooks hooks_;
  std::vector<int> saved_, preview_values_;
  int saved_preset_, preview_preset_ = -1;
  bg3::settings::KVList preview_keys_;
  std::vector<std::string> changes_, blocked_;
  std::string preview_kind_, status_, game_path_, report_hint_, mod_status_ = "Not checked";
  int restart_count_ = 0;
  bool can_play_ = false, report_visible_ = false, pending_play_ = false;
  GraphicsInspection inspection_;
  Compatibility compatibility_;
  int selected_ = -1;
  diagnostics::Snapshot report_;
};
}
