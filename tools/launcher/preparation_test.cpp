// tools/launcher/preparation.h: launcher-only settings, the card's lines and the run policy. CPU-only; starts no
// process (the Manager's children are exercised by hand, docs/RTX_HAIR_ROADMAP.md 2b).
#include "preparation.h"
#include <cstdio>
#include <string>

using namespace launcher::prep;

int main() {
  int failed = 0, checks = 0;
  auto check = [&](bool ok, const char* what) { ++checks; if (!ok) { ++failed; std::printf("FAIL %s\n", what); } };
  // Settings: parsed, merged with every other line kept (game_dir), defaults off.
  Settings d = parse_settings("");
  check(!d.convert_all && !d.while_playing && !d.paused, "defaults off");
  const std::string ini = "# BG3RTX.exe: the game folder\ngame_dir=C:\\Game\nhair_python=C:\\Py\\python.exe\n";
  Settings s;
  s.convert_all = true; s.paused = true;
  const std::string merged = merge_settings(ini, s);
  check(merged.find("game_dir=C:\\Game\n") != std::string::npos && merged.find("hair_python=C:\\Py\\python.exe") != std::string::npos,
        "merge keeps other keys");
  Settings back = parse_settings(merged);
  check(back.convert_all && back.paused && !back.while_playing, "merge round trip");
  check(merge_settings(merged, back) == merged, "merge is idempotent (no duplicate keys)");
  // JSON fields of the converter's preparation.json (Python json.dumps spacing).
  const std::string status = "{\"kind\": \"hair_grooms\", \"ready\": 412, \"finished\": 430, \"needed\": 559, \"bytes\": 2100000000, "
                             "\"state\": \"running\", \"detail\": \"Converting named characters, about 35 min left.\"}";
  double v = 0;
  check(json_number(status, "ready", v) && v == 412, "json number");
  check(!json_number(status, "missing", v) && json_string(status, "detail") == "Converting named characters, about 35 min left.",
        "json string");
  // Hair card lines.
  HairInput hi;
  hi.missing = "Python 3 was not found";
  Item it = hair_item(hi, d);
  check(it.state == "unavailable" && it.line == "Not available: Python 3 was not found.", "unavailable");
  hi = {};
  it = hair_item(hi, d);
  check(it.state == "waiting" && it.line == "Not converted yet." && it.progress == 0, "not converted yet");
  hi.status_json = status; hi.running = true;
  it = hair_item(hi, d);
  check(it.state == "running" && it.line == "412 of 559 styles ready, 2.1 GB on disk." &&
        it.detail == "Converting named characters, about 35 min left." && it.progress > 0.76 && it.progress < 0.78, "running");
  hi.game_running = true;
  check(hair_item(hi, d).detail == "Converting named characters, about 35 min left. Using one core while you play.", "playing");
  hi.running = false;
  check(hair_item(hi, d).detail == "Waits until the game is closed.", "waits for the game");
  hi.game_running = false;
  Settings paused; paused.paused = true;
  check(hair_item(hi, paused).state == "paused", "paused");
  hi.failed = true; hi.error = "LSLib not found";
  check(hair_item(hi, d).detail == "Conversion stopped: LSLib not found", "failed");
  hi = {};
  hi.status_json = "{\"ready\": 5, \"finished\": 5, \"needed\": 5, \"bytes\": 50000000}";
  it = hair_item(hi, d);
  check(it.state == "done" && it.line == "5 of 5 styles ready, 50 MB on disk.", "done");
  // Shader card lines; the driver note is always there.
  ShaderInput si;
  check(shader_item(si).state == "unavailable", "shaders unavailable");
  si.available = true; si.running = true; si.status_json = "{\"done\": 10, \"total\": 71, \"compiled\": 3, \"failed\": 0, \"state\": \"running\"}";
  it = shader_item(si);
  check(it.state == "running" && it.line == "Preparing shaders: 10 of 71." && it.detail.find("first ray tracing pipeline") != std::string::npos,
        "shaders running");
  si.running = false; si.status_json = "{\"done\": 71, \"total\": 71, \"compiled\": 0, \"failed\": 0, \"state\": \"done\"}";
  check(shader_item(si).line == "Ready.", "shaders cached");
  si.status_json = "{\"done\": 71, \"total\": 71, \"compiled\": 0, \"failed\": 2, \"state\": \"done\"}";
  check(shader_item(si).line == "Ready. 2 could not be compiled; the game retries them.", "shader failures");
  // Policy: jobs and when to run.
  check(jobs_for(true, 32) == 1 && jobs_for(false, 32) == 4 && jobs_for(false, 4) == 1, "jobs");
  Settings playing; playing.while_playing = true;
  check(hair_should_run(d, true, false, false, false) && !hair_should_run(d, true, true, false, false) &&
        hair_should_run(playing, true, true, false, false) && !hair_should_run(paused, true, false, false, false) &&
        !hair_should_run(d, false, false, false, false) && !hair_should_run(d, true, false, true, false) &&
        !hair_should_run(d, true, false, false, true), "run policy");
  // JSON for the bridge.
  const std::string j = to_json({it}, playing, true);
  check(j.find("{\"items\":[{\"id\":\"shaders\"") == 0 && j.find("\"whilePlaying\":true") != std::string::npos &&
        j.find("\"pausable\":true") != std::string::npos && j.find("\"convertAll\":false") != std::string::npos, "json shape");
  std::printf("preparation: %d checks, %d failed\n", checks, failed);
  return failed ? 1 : 0;
}
