#pragma once
// First-run preparation in the launcher (docs/RTX_HAIR_ROADMAP.md 2b): the strand groom conversion
// (python -m tools.hair_groom.batch) and the shader compile before Play (BG3RTX.exe --precompile-shaders, which calls
// the layer's bg3rtx_precompile). Both run as BelowNormal child processes in job objects, so closing the launcher
// stops them and their children. Launcher-only settings live in bg3rtx_launcher.ini (never game keys).
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace launcher::prep {

struct Settings {
  bool convert_all = false;     // hair_convert_all: also the styles no game file uses
  bool while_playing = false;   // hair_while_playing: keep converting (fewer jobs) while the game runs
  bool paused = false;          // prep_paused
};
Settings parse_settings(const std::string& ini);
// The ini text with these keys set; every other line is kept.
std::string merge_settings(const std::string& ini, const Settings& s);

struct Item {
  std::string id, title, line, detail, state;   // state: done, running, waiting, paused, unavailable, failed
  double progress = -1;                         // 0..1, or -1 for none
};

// One numeric / string field of a flat JSON object (the converter's preparation.json, the precompile status).
bool json_number(const std::string& json, const char* key, double& out);
std::string json_string(const std::string& json, const char* key);

struct HairInput {
  std::string missing;           // non-empty: the converter cannot run (what is missing)
  std::string status_json;       // <report dir>\preparation.json, may be empty
  bool running = false, game_running = false, failed = false;
  std::string error;             // the batch's last output line after a failure
};
Item hair_item(const HairInput& in, const Settings& s);

struct ShaderInput {
  bool available = false;        // layer DLL and shaders present
  std::string status_json;       // precompile.json, may be empty
  bool running = false, game_running = false, started = false;
};
Item shader_item(const ShaderInput& in);

// Conversion jobs: closed game: a quarter of the logical cores (1..4, the batch's default); playing: 1.
int jobs_for(bool game_running, unsigned cores);
// Whether the conversion should run now.
bool hair_should_run(const Settings& s, bool converter_ok, bool game_running, bool done_this_session, bool failed);

std::string to_json(const std::vector<Item>& items, const Settings& s, bool hair_has_work);

// The live part (web_host): child processes and the files they write. tick() from the UI timer.
class Manager {
 public:
  Manager(std::wstring root, std::wstring game_dir);
  ~Manager();
  void tick(bool game_running);
  // Actions from the card: pause, resume, convertAll (0/1), whilePlaying (0/1). False: unknown action.
  bool act(const std::string& action, int value);
  std::string json() const { return json_; }
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::string json_;
};

// BG3RTX.exe --precompile-shaders <status json>: loads the layer, compiles the uncached requests, writes progress.
int precompile_main(const std::wstring& status_path, const std::wstring& list_override = {});

}  // namespace launcher::prep
