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
  hi.no_captures = true;
  it = hair_item(hi, d);
  check(it.state == "waiting" && it.line == "No hair styles captured yet." && it.progress < 0, "no captures yet");
  hi.no_captures = false;
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
        j.find("\"pausable\":true") != std::string::npos && j.find("\"convertAll\":false") != std::string::npos &&
        j.find("\"hair\":{}") != std::string::npos && j.find("\"granny\":{\"path\":\"\",\"ok\":false}") != std::string::npos,
        "json shape");
  // RTX Hair panel (docs/RTX_HAIR_ROADMAP.md 2f): the player's granny2.dll, the numbers, Convert all styles.
  check(hair_item(HairInput{}, d).title == "RTX Hair", "user text says RTX Hair");
  Settings g; g.granny = "C:\\Tools\\ExportTool\\granny2.dll";
  const std::string gm = merge_settings(ini, g);
  check(parse_settings(gm).granny == g.granny && merge_settings(gm, parse_settings(gm)) == gm, "granny path round trip");
  check(merge_settings("", d).find("hair_granny") == std::string::npos, "no granny line when unset");
  check(granny_name_ok(L"C:\\x\\Granny2.DLL") && granny_name_ok(L"granny2.dll") && !granny_name_ok(L"C:\\x\\granny2.dll.txt") &&
        !granny_name_ok(L"C:\\granny2.dll\\other.dll"), "granny file name");
  check(convert_all_reason("", false, true) == "Needs granny2.dll: choose it below." &&
        convert_all_reason("C:\\g\\granny2.dll", false, true).find("not found") != std::string::npos &&
        convert_all_reason("C:\\g\\granny2.dll", true, false).find("game folder") != std::string::npos &&
        convert_all_reason("C:\\g\\granny2.dll", true, true).empty(), "convert all reason");
  check(hair_command(L"C:\\b\\conv.exe", L"C:\\cap", L"C:\\out", L"C:\\rep", 4, false, L"C:\\Game", L"") ==
            L"\"C:\\b\\conv.exe\" \"C:\\cap\" --out \"C:\\out\" --report-dir \"C:\\rep\" --threads 4 --jobs 1",
        "captures command");
  check(hair_command(L"C:\\b\\conv.exe", L"C:\\cap", L"C:\\out", L"C:\\rep", 1, true, L"C:\\Game", L"C:\\g\\granny2.dll") ==
            L"\"C:\\b\\conv.exe\" --all --game \"C:\\Game\" --granny \"C:\\g\\granny2.dll\" --out \"C:\\out\" --report-dir \"C:\\rep\" --threads 1 --jobs 1",
        "convert all command");
  // Facts tolerate missing fields (an older converter has no total or source) and report the captures and errors.
  HairInput fi;
  fi.status_json = status; fi.captured = 430;
  HairFacts f = hair_facts(fi, d);
  check(f.ready == 412 && f.finished == 430 && f.needed == 559 && f.total == -1 && f.captured == 430 && f.bytes == 2100000000 &&
        f.eta_s == -1 && f.source == "seen" && f.error.empty(), "facts without total");
  fi.status_json = "{\"ready\": 3, \"total\": 1530, \"source\": \"all\", \"eta_s\": 7300, \"error\": \"pak not found\"}";
  f = hair_facts(fi, d);
  check(f.total == 1530 && f.source == "all" && f.eta_s == 7300 && f.error == "pak not found" && f.needed == -1, "facts with total");
  fi.failed = true; fi.error = "granny2.dll could not be loaded";
  check(hair_facts(fi, d).error == "granny2.dll could not be loaded", "a failed run's error wins");
  fi = {}; fi.status_json = "{\"mode\": \"all\"}";
  check(hair_facts(fi, d).source == "all" && hair_facts(HairInput{}, g).source == "seen", "source from mode, else the setting");
  Extra ex{f, true, ""};
  const std::string jf = to_json({it}, g, true, ex);
  check(jf.find("\"hair\":{\"ready\":3,\"captured\":430,\"total\":1530,") != std::string::npos && jf.find("\"etaSeconds\":7300") != std::string::npos &&
        jf.find("\"source\":\"all\"") != std::string::npos && jf.find("\"finished\"") == std::string::npos &&
        jf.find("\"granny\":{\"path\":\"C:\\\\Tools\\\\ExportTool\\\\granny2.dll\",\"ok\":true}") != std::string::npos &&
        jf.find("\"convertAllReason\":\"\"") != std::string::npos, "json facts");
  std::printf("preparation: %d checks, %d failed\n", checks, failed);
  return failed ? 1 : 0;
}
