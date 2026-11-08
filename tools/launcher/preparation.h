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
  bool convert_all = false;     // hair_convert_all: every style in the game paks (needs granny2.dll), not only captures
  bool while_playing = false;   // hair_while_playing: keep converting (fewer jobs) while the game runs
  bool paused = false;          // prep_paused
  std::string granny;           // hair_granny: the player's own granny2.dll (LSLib ExportTool); never shipped
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
  std::string error;             // the converter's last output line after a failure
  bool no_captures = false;      // the game has not captured a style without a groom yet
  int captured = -1;             // .hcap files the layer wrote (-1: unknown)
};
Item hair_item(const HairInput& in, const Settings& s);

// The RTX Hair panel's numbers. Every field is optional: -1 / empty when the converter's status does not have it
// (older converters write no total or source). docs/RTX_HAIR_ROADMAP.md 2f: "total" counts every style of the
// game paks once a "Convert all styles" run has listed them; "source" is "seen" (captures) or "all" (paks).
struct HairFacts {
  int ready = -1, captured = -1, total = -1, finished = -1, needed = -1;
  double bytes = -1, eta_s = -1;
  std::string source, error;
};
HairFacts hair_facts(const HairInput& in, const Settings& s);

// Why "Convert all styles" cannot run, in one line ("" when it can).
std::string convert_all_reason(const std::string& granny, bool granny_ok, bool game_ok);
// The converter's command line. Captures mode: <capture dir> --out ...; all mode: --all --game <dir> --granny <dll>.
std::wstring hair_command(const std::wstring& converter, const std::wstring& capture_dir, const std::wstring& groom_dir,
                          const std::wstring& report_dir, int threads, bool all, const std::wstring& game,
                          const std::wstring& granny);
// A plausible granny2.dll: the file name only (the launcher cannot check the DLL itself).
bool granny_name_ok(const std::wstring& path);

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

struct Extra {
  HairFacts hair;
  bool granny_ok = false;
  std::string all_reason;        // convert_all_reason()
};
std::string to_json(const std::vector<Item>& items, const Settings& s, bool hair_has_work, const Extra& extra = {});

// The live part (web_host): child processes and the files they write. tick() from the UI timer.
class Manager {
 public:
  Manager(std::wstring root, std::wstring game_dir);
  ~Manager();
  void tick(bool game_running);
  // Actions from the card: start, pause, resume, convertAll (0/1), whilePlaying (0/1), clearGranny.
  // False: unknown action or not possible (convertAll 1 without a usable granny2.dll).
  bool act(const std::string& action, int value);
  // The player's granny2.dll from the host's file picker. False: not a granny2.dll (nothing changes).
  bool set_granny(const std::wstring& path);
  std::wstring granny() const;
  std::string json() const { return json_; }
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::string json_;
};

// BG3RTX.exe --precompile-shaders <status json>: loads the layer, compiles the uncached requests, writes progress.
int precompile_main(const std::wstring& status_path, const std::wstring& list_override = {});

}  // namespace launcher::prep
