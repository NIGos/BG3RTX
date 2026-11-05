// BG3RTX.exe: the player's settings window and launcher (docs/USER_SETTINGS.md).
//
//   BG3RTX.exe                         settings window
//   BG3RTX.exe --launch                start the game with the layer, no window
//   BG3RTX.exe --launch --dry-run      print what would be started (exe, args, environment)
//   BG3RTX.exe --preset ultra          write bg3rtx_user.ini with a preset (limited to the GPU)
//   BG3RTX.exe --detect                GPU capabilities and the suggested preset
//   BG3RTX.exe --dump-schema           the settings table as JSON
//   Options: --game <folder> --exe <program> --clean --no-clamp --out <file> -- <game arguments>
//   Steam launch option:  "<path>\bin\BG3RTX.exe" %command%
//
// The WebView UI and native Win32 fallback share the settings table in common/settings.h.
#include "launcher.h"
#include "web_host.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <cstdio>

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using namespace launcher;
namespace st = bg3::settings;

// ---- command line output ----

static std::wstring g_out;
static std::wstring g_out_file;

static void emit(const std::wstring& s) { g_out += s; }

static void flush_output() {
  if (g_out.empty()) return;
  std::string utf8 = narrow(g_out);
  if (!g_out_file.empty()) {
    write_file_atomic(g_out_file, utf8, nullptr);
    return;
  }
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (!h || h == INVALID_HANDLE_VALUE) {
    if (AttachConsole(ATTACH_PARENT_PROCESS)) h = GetStdHandle(STD_OUTPUT_HANDLE);
  }
  if (!h || h == INVALID_HANDLE_VALUE) {
    MessageBoxW(nullptr, g_out.c_str(), L"BG3RTX", MB_OK);
    return;
  }
  DWORD n;
  WriteFile(h, utf8.data(), DWORD(utf8.size()), &n, nullptr);
}

static void detect_gpu(State& s, std::wstring* err) {
  std::vector<st::Gpu> gpus = probe_gpus(err);
  int b = best_gpu(gpus);
  if (b < 0) return;
  s.gpu = gpus[b];
  s.have_gpu = true;
  s.caps = st::gpu_caps(s.gpu);
  s.suggested = st::suggest_preset(s.gpu);
}

static std::wstring gpu_line(const State& s, const std::wstring& err) {
  if (!s.have_gpu) return L"Graphics compatibility could not be checked: " + err + L". Unverified features are unavailable; recheck in Help & support.";
  return widen(s.gpu.name) + L": " + caps_text(s.caps) + L". Suggested preset: " +
         widen(st::kPresetLabel[s.suggested]) + L".";
}

// ======================================================================
// Settings window
// ======================================================================

#include "ui_product.h"

// ======================================================================
// Command line
// ======================================================================

static bool ends_with_exe(const std::wstring& a) {
  return a.size() > 4 && _wcsicmp(a.c_str() + a.size() - 4, L".exe") == 0;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  bool do_launch = false, dry = false, detect = false, dump = false, clean = false, clamp = true, report = false, native = false;
  std::wstring game_dir, exe, preset;
  std::vector<std::wstring> game_args;
  for (int i = 1; i < argc; ++i) {
    std::wstring a = argv[i];
    auto next = [&]() { return i + 1 < argc ? std::wstring(argv[++i]) : std::wstring(); };
    if (a == L"--") {
      for (++i; i < argc; ++i) game_args.push_back(argv[i]);
    } else if (a == L"--launch") do_launch = true;
    else if (a == L"--dry-run") dry = true;
    else if (a == L"--detect") detect = true;
    else if (a == L"--report") report = true;
    else if (a == L"--native-ui") native = true;
    else if (a == L"--dump-schema") dump = true;
    else if (a == L"--clean") clean = true;
    else if (a == L"--no-clamp") clamp = false;
    else if (a == L"--game") game_dir = next();
    else if (a == L"--exe") exe = next();
    else if (a == L"--preset") preset = next();
    else if (a == L"--out") g_out_file = next();
    else if (a == L"--no-game") g_no_game=true;
    else if (a == L"--screenshot-width") g_review_width=_wtoi(next().c_str());
    else if (a == L"--screenshot-view") g_review=next();
    else if (a == L"--screenshot") g_screenshot = next();
    else if (a == L"--screenshot-bottom") g_screenshot_bottom=true;
    else if (a == L"--screenshot-scale") g_screenshot_scale=float(_wtof(next().c_str()));
    else if (a == L"--help" || a == L"-h" || a == L"/?") {
      emit(L"BG3RTX.exe [--launch [--dry-run]] [--preset low|medium|high|ultra [--no-clamp]] [--detect]\n"
           L"           [--report] [--native-ui] [--no-game --screenshot <PNG> [--screenshot-view <view>] [--screenshot-scale <s>] [--screenshot-bottom]]\n"
           L"           [--dump-schema] [--game <folder>] [--exe <program>] [--clean] [--out <file>] [-- <game args>]\n"
           L"Steam launch option: \"<path>\\bin\\BG3RTX.exe\" %command%\n");
      flush_output();
      return 0;
    } else if (ends_with_exe(a) && exe.empty()) {
      // Steam's %command%: "<game>\Launcher\LariLauncher.exe" [args]. Start
      // bin\bg3.exe (Vulkan) of that install with the remaining arguments.
      do_launch = true;
      if (game_dir.empty()) game_dir = game_dir_from_path(a);
      for (++i; i < argc; ++i) game_args.push_back(argv[i]);
    } else {
      emit(L"unknown argument: " + a + L"\n");
      flush_output();
      return 2;
    }
  }
  LocalFree(argv);
  if ((!g_screenshot.empty() || g_no_game) && (do_launch || !preset.empty() || detect || dump)) return 2;
  if ((!g_review.empty() || g_review_width) && g_screenshot.empty()) return 2;
  if (g_review_width && (g_review_width<720 || g_review_width>1920)) return 2;
  if((g_screenshot_bottom || g_screenshot_scale!=0) && (g_screenshot.empty() || do_launch || detect || dump || !preset.empty() || g_screenshot_scale<0 || g_screenshot_scale>3))return 2;

  if (report && (do_launch || detect || dump || !preset.empty() || !g_screenshot.empty())) return 2;
  if (!do_launch && !detect && !dump && preset.empty()) {
    if (!native) {
      int result = launcher::web::run(inst, game_dir, report, g_no_game, g_screenshot,
                                     g_review_width ? g_review_width : 1180, g_review,
                                     g_screenshot_scale, g_screenshot_bottom);
      if (result != -1) return result;
    }
    if (report) { CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int rc=launcher::web::native_report(nullptr);CoUninitialize();return rc; }
    return run_window(inst, game_dir);
  }

  int rc = 0;
  if (dump) emit(widen(st::schema_json()));
  State s;
  load_state(s);
  std::wstring err;
  if (detect || !preset.empty()) detect_gpu(s, &err);
  if (detect) {
    std::vector<st::Gpu> gpus = probe_gpus(&err);
    for (auto& gp : gpus) {
      wchar_t buf[256];
      swprintf(buf, 256, L"gpu: %ls (vendor 0x%04X device 0x%04X)%ls, ray query %ls, RTX series %d\n",
               widen(gp.name).c_str(), gp.vendor, gp.device, gp.discrete ? L" discrete" : L"",
               gp.ray_query ? L"yes" : L"no", st::nv_series(gp));
      emit(buf);
    }
    emit(L"selected: " + gpu_line(s, err) + L"\n");
    emit(L"suggested preset: " + widen(st::kPresetId[s.suggested]) + L"\n");
  }
  if (!preset.empty()) {
    int p = st::preset_from_id(narrow(preset));
    if (p < 0) {
      emit(L"unknown preset: " + preset + L"\n");
      rc = 2;
    } else {
      s.preset = p;
      s.values = clamp ? st::preset_values_for(p, s.caps) : st::preset_values(p);
      if (!clamp) s.have_gpu = false;   // no forced hardware keys either
      if (save_state(s, &err)) emit(L"wrote " + user_ini_path() + L" (preset " + preset + L")\n");
      else emit(L"error: " + err + L"\n"), rc = 1;
    }
  }
  if (do_launch && rc == 0) {
    if (game_dir.empty()) game_dir = saved_game_dir();
    if (!valid_game_dir(game_dir)) game_dir = detect_game_dir();
    if (exe.empty() && !valid_game_dir(game_dir)) {
      emit(L"error: Baldur's Gate 3 not found; pass --game <folder> (the one that contains bin\\bg3.exe)\n");
      rc = 1;
    } else {
      LaunchPlan p = make_plan(game_dir, exe, game_args, clean, effective_fg(s));
      if (dry) {
        emit(describe(p));
      } else if (exe.empty() && game_running()) {
        emit(L"the game is already running\n");
        rc = 1;
      } else {
        DWORD pid = 0;
        if (exe.empty()) {
          std::wstring note = guard_game_upscaler(s);
          if (!note.empty()) emit(note + L"\n");
        }
        if (launch(p, &pid, &err)) emit(L"started " + p.exe + L" (pid " + std::to_wstring(pid) + L")\n");
        else emit(L"error: " + err + L"\n"), rc = 1;
      }
    }
  }
  flush_output();
  return rc;
}
