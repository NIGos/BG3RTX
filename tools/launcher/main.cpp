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
// The window is plain Win32 + common controls, generated from the shared
// table in common/settings.h (the phase-2 overlay draws the same table).
#include "launcher.h"

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
  if (!s.have_gpu) return L"GPU detection failed: " + err + L". Every setting is shown.";
  return widen(s.gpu.name) + L": " + caps_text(s.caps) + L". Suggested preset: " +
         widen(st::kPresetLabel[s.suggested]) + L".";
}

// ======================================================================
// Settings window
// ======================================================================

enum : int {
  IDC_GPU = 10, IDC_PRESET, IDC_SUGGEST, IDC_PRESET_NOTE, IDC_DESC_TITLE, IDC_DESC, IDC_GAMEDIR, IDC_BROWSE,
  IDC_CLEAN, IDC_RESET, IDC_SAVE, IDC_LAUNCH, IDC_CLOSE, IDC_STATUS, IDC_ADVANCED, IDC_EXPERIMENTAL,
  IDC_KNOB = 1000,    // + knob index: combo box or trackbar
  IDC_VALUE = 1100,   // slider value text
  IDC_BADGE = 1200,
  IDC_LABEL = 1300,
};

struct Ui {
  HWND wnd = nullptr;
  HFONT font = nullptr, bold = nullptr;
  float s = 1.0f;
  State st;
  std::wstring gpu_err;
  bool updating = false;
  int content_height = 0, scroll_y = 0;
  int desc_knob = -2;   // -1 preset, >= 0 knob
  COLORREF badge_color[st::kKnobCount] = {};
  HBRUSH bg = nullptr;
  bool dev = false;   // dev=1 in the merged ini: developer-only knobs and choices are shown
};
static Ui g;

static int S(int v) { return int(v * g.s + 0.5f); }

static HWND add(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id,
                bool bold = false) {
  HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), g.wnd,
                           reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
  SendMessageW(c, WM_SETFONT, WPARAM(bold ? g.bold : g.font), TRUE);
  return c;
}

static HWND item(int id) { return GetDlgItem(g.wnd, id); }

// Trackbar position <-> slider value (has_off: one position below min means Off).
static int slider_pos(const st::Knob& k, int v) {
  if (k.slider.has_off && v == 0) return k.slider.min - 1;
  return v < 0 ? k.slider.max : v;
}
static int slider_from_pos(const st::Knob& k, int pos) {
  if (k.slider.has_off && pos < k.slider.min) return 0;
  int v = k.slider.min + ((pos - k.slider.min + k.slider.step / 2) / k.slider.step) * k.slider.step;
  return std::min(std::max(v, k.slider.min), k.slider.max);
}
static std::wstring slider_text(const st::Knob& k, int v) {
  if (v < 0) return L"custom";
  if (k.slider.has_off && v == 0) return L"Off";
  return std::to_wstring(v) + L" " + widen(k.slider.unit);
}

static void show_desc(int knob) {
  if (knob == g.desc_knob) return;
  g.desc_knob = knob;
  if (knob < 0) {
    int p = g.st.preset;
    SetWindowTextW(item(IDC_DESC_TITLE), (L"Preset: " + widen(st::kPresetLabel[p])).c_str());
    SetWindowTextW(item(IDC_DESC), widen(st::kPresetDesc[p]).c_str());
    return;
  }
  const st::Knob& k = st::kKnobs[knob];
  SetWindowTextW(item(IDC_DESC_TITLE), widen(k.label).c_str());
  std::wstring d = widen(k.desc);
  if (k.apply == st::Apply::Live) d += L"\r\n\r\nApplies while the game runs (within a second).";
  else if (k.apply == st::Apply::Hitch) d += L"\r\n\r\nApplies while the game runs, with a brief hitch.";
  else d += L"\r\n\r\n" + widen(k.apply_note) + L"; other changes apply while the game runs.";
  SetWindowTextW(item(IDC_DESC), d.c_str());
}

static st::Map effective_settings() {
  st::Map map;st::apply_layer(map,g.st.base);st::apply_layer(map,g.st.user);
  for(int i=0;i<st::kKnobCount;++i)if(g.st.values[i]>=0){st::KVList values;st::knob_kvs(st::kKnobs[i],g.st.values[i],values);for(const auto& kv:values)map[kv.first]=kv.second;}
  return map;
}

// The settings of the saved files (what the game starts from), for the restart markers.
static st::Map saved_settings() {
  st::Map map;st::apply_layer(map,g.st.base);st::apply_layer(map,g.st.user);
  return map;
}

static void refresh() {
  g.updating = true;
  State& s = g.st;
  const auto effective=effective_settings();
  const auto saved=saved_settings();
  SendMessageW(item(IDC_PRESET), CB_SETCURSEL, s.preset, 0);
  std::vector<int> pv = st::preset_values_for(s.preset, s.caps);
  int changed = 0;
  for (int i = 0; i < st::kKnobCount; ++i) changed += s.values[i] != pv[i];
  std::wstring note = changed ? L"Custom: " + std::to_wstring(changed) + L" setting" + (changed > 1 ? L"s" : L"") +
                                    L" changed from " + widen(st::kPresetLabel[s.preset])
                              : L"Matches the preset";
  SetWindowTextW(item(IDC_PRESET_NOTE), note.c_str());

  for (int i = 0; i < st::kKnobCount; ++i) {
    const st::Knob& k = st::kKnobs[i];
    int v = s.values[i];
    if (!item(IDC_KNOB + i)) continue;   // developer-only knob, hidden (dev=0): its value is kept
    if (k.kind == st::Kind::Choice) {
      SendMessageW(item(IDC_KNOB + i), CB_SETCURSEL, v, 0);   // -1: no selection (hand-edited)
      bool any = false;
      for (int c = 1; c < st::choice_count(k); ++c) any |= st::choice_supported(k, c, s.caps);
      EnableWindow(item(IDC_KNOB + i), any || v > 0);
    } else {
      SendMessageW(item(IDC_KNOB + i), TBM_SETPOS, TRUE, slider_pos(k, v));
      SetWindowTextW(item(IDC_VALUE + i), slider_text(k, v).c_str());
    }
    // Badge: unmet dependency / unsupported / restart required / how it applies.
    std::wstring badge;
    COLORREF col = RGB(110, 110, 110);
    std::string need = st::unmet_need(k, v, s.values);
    if(need.empty())need=st::unmet_key_need(k,v,effective);
    // Restart: a key this choice writes changes from the saved files' value in a way only a game
    // start applies (also right after a migrated file, whose level writes keys the file lacked).
    bool restart = false;
    if (v >= 0) {
      st::KVList to;
      st::knob_kvs(k, v, to);
      for (auto& t : to)
        if (st::key_change_needs_restart(t.first, st::lookup(saved, t.first), t.second)) restart = true;
    }
    if (v < 0) badge = L"hand-edited in the ini (kept)";
    else if (!need.empty()) badge = widen(need), col = RGB(200, 30, 30);
    else if (!st::choice_supported(k, v, s.caps)) badge = L"not supported on this GPU", col = RGB(200, 30, 30);
    else if (restart) badge = L"restart required", col = RGB(210, 110, 0);
    else if (k.apply == st::Apply::Hitch) badge = L"live, brief hitch";
    else if (k.apply == st::Apply::Partial || k.apply == st::Apply::Restart) badge = widen(k.apply_note);
    else badge = L"live";
    g.badge_color[i] = col;
    SetWindowTextW(item(IDC_BADGE + i), badge.c_str());
  }
  g.updating = false;
}

static void status(const std::wstring& t) { SetWindowTextW(item(IDC_STATUS), t.c_str()); }

static void scroll_to(int requested) {
  RECT client{};GetClientRect(g.wnd,&client);
  const int next=std::clamp(requested,0,std::max(0,g.content_height-int(client.bottom)));
  if(next!=g.scroll_y){ScrollWindowEx(g.wnd,0,g.scroll_y-next,nullptr,nullptr,nullptr,nullptr,SW_SCROLLCHILDREN|SW_INVALIDATE|SW_ERASE);g.scroll_y=next;}
  SCROLLINFO info{sizeof info,SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,g.content_height-1),UINT(client.bottom),g.scroll_y};
  SetScrollInfo(g.wnd,SB_VERT,&info,TRUE);
}

static void build_window() {
  State& s = g.st;
  const int L = 16, CTRL = 216, BADGE = 446, DESC = 640, ROW = 30;
  int y = 12;
  add(L"STATIC", gpu_line(s, g.gpu_err).c_str(), SS_LEFT, L, y, 900, 20, IDC_GPU);
  y += 30;
  add(L"STATIC", L"Preset", SS_LEFT, L, y + 4, 190, 20, 0, true);
  HWND pc = add(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, CTRL, y, 210, 200, IDC_PRESET);
  for (int p = 0; p < st::kPresetCount; ++p) SendMessageW(pc, CB_ADDSTRING, 0, LPARAM(widen(st::kPresetLabel[p]).c_str()));
  add(L"BUTTON", L"Use suggested", BS_PUSHBUTTON | WS_TABSTOP, BADGE, y - 1, 130, 26, IDC_SUGGEST);
  y += 30;
  add(L"STATIC", L"", SS_LEFT, CTRL, y, 400, 20, IDC_PRESET_NOTE);
  y += 34;
  int desc_top = y;

  bool adv_done = false, exp_done = false, dev_done = false;
  for (int i = 0; i < st::kKnobCount; ++i) {
    const st::Knob& k = st::kKnobs[i];
    if (!st::knob_visible(k, g.dev)) continue;
    if (k.advanced && !k.experimental && !adv_done) {
      adv_done = true;
      y += 8;
      add(L"STATIC", L"Advanced", SS_LEFT, L, y, 190, 20, IDC_ADVANCED, true);
      y += 26;
    }
    if (k.experimental && !exp_done) {
      exp_done = true;
      y += 8;
      add(L"STATIC", L"Experimental (being tested)", SS_LEFT, L, y, 300, 20, IDC_EXPERIMENTAL, true);
      y += 26;
    }
    if (k.dev_only && !dev_done) {
      dev_done = true;
      y += 8;
      add(L"STATIC", L"Developer (dev=1)", SS_LEFT, L, y, 300, 20, 0, true);
      y += 26;
    }
    add(L"STATIC", widen(k.label).c_str(), SS_LEFT, L, y + 4, 196, 20, IDC_LABEL + i);
    if (k.kind == st::Kind::Choice) {
      HWND c = add(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, CTRL, y, 210, 200, IDC_KNOB + i);
      for (int ch = 0; ch < st::choice_count(k); ++ch) {
        // Developer-only choices are the last ones: hidden unless dev=1 or already selected.
        if (k.choices[ch].dev && !g.dev && s.values[i] < ch) break;
        std::wstring label = widen(k.choices[ch].label);
        if (!st::choice_supported(k, ch, s.caps)) label += L" (not supported)";
        SendMessageW(c, CB_ADDSTRING, 0, LPARAM(label.c_str()));
      }
    } else {
      HWND t = add(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, CTRL, y, 150, 26, IDC_KNOB + i);
      SendMessageW(t, TBM_SETRANGEMIN, FALSE, k.slider.has_off ? k.slider.min - 1 : k.slider.min);
      SendMessageW(t, TBM_SETRANGEMAX, TRUE, k.slider.max);
      SendMessageW(t, TBM_SETLINESIZE, 0, k.slider.step);
      SendMessageW(t, TBM_SETPAGESIZE, 0, k.slider.step * (k.slider.max - k.slider.min > 100 ? 10 : 2));
      add(L"STATIC", L"", SS_LEFT, CTRL + 154, y + 4, 60, 20, IDC_VALUE + i);
    }
    add(L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, BADGE, y + 4, 186, 20, IDC_BADGE + i);
    y += ROW;
  }

  // Description panel on the right.
  add(L"STATIC", L"", SS_LEFT, DESC, desc_top, 280, 20, IDC_DESC_TITLE, true);
  add(L"STATIC", L"", SS_LEFT, DESC, desc_top + 26, 280, y - desc_top - 26, IDC_DESC);

  y += 14;
  add(L"STATIC", L"Game folder", SS_LEFT, L, y + 4, 190, 20, 0, true);
  add(L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, CTRL, y, 560, 24, IDC_GAMEDIR);
  add(L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, CTRL + 570, y - 1, 120, 26, IDC_BROWSE);
  y += 34;
  add(L"BUTTON", L"Disable other Vulkan overlays (Steam, EOS, GOG) for this launch", BS_AUTOCHECKBOX | WS_TABSTOP,
      CTRL, y, 560, 22, IDC_CLEAN);
  y += 36;
  add(L"BUTTON", L"Reset to preset", BS_PUSHBUTTON | WS_TABSTOP, L, y, 150, 30, IDC_RESET);
  add(L"BUTTON", L"Save", BS_PUSHBUTTON | WS_TABSTOP, 530, y, 110, 30, IDC_SAVE);
  add(L"BUTTON", L"Save and launch", BS_DEFPUSHBUTTON | WS_TABSTOP, 650, y, 150, 30, IDC_LAUNCH);
  add(L"BUTTON", L"Close", BS_PUSHBUTTON | WS_TABSTOP, 810, y, 110, 30, IDC_CLOSE);
  y += 42;
  add(L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, L, y, 904, 20, IDC_STATUS);
  y += 30;

  g.content_height=S(y);
  MONITORINFO monitor{sizeof monitor};GetMonitorInfoW(MonitorFromWindow(g.wnd,MONITOR_DEFAULTTOPRIMARY),&monitor);
  const int available=std::max(1,int(monitor.rcWork.bottom-monitor.rcWork.top)-S(52));
  RECT r{0, 0, S(936), std::min(g.content_height,available)};
  AdjustWindowRectEx(&r, GetWindowLongW(g.wnd, GWL_STYLE), FALSE, 0);
  SetWindowPos(g.wnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  scroll_to(0);
}

static std::wstring game_dir_text() {
  wchar_t buf[MAX_PATH * 2] = {};
  GetWindowTextW(item(IDC_GAMEDIR), buf, int(std::size(buf)));
  std::string t = st::trim(narrow(buf));
  return widen(t);
}

static void browse() {
  IFileOpenDialog* dlg = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
  DWORD opts = 0;
  dlg->GetOptions(&opts);
  dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
  dlg->SetTitle(L"Baldur's Gate 3 folder (the one that contains bin\\bg3.exe)");
  if (SUCCEEDED(dlg->Show(g.wnd))) {
    IShellItem* it = nullptr;
    if (SUCCEEDED(dlg->GetResult(&it))) {
      wchar_t* path = nullptr;
      if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        std::wstring d = game_dir_from_path(path);
        SetWindowTextW(item(IDC_GAMEDIR), (d.empty() ? std::wstring(path) : d).c_str());
        status(d.empty() ? L"bin\\bg3.exe not found in that folder" : L"Game folder set");
        CoTaskMemFree(path);
      }
      it->Release();
    }
  }
  dlg->Release();
}

static bool save(bool quiet) {
  // Dependencies are enforced: a choice whose required setting is off is lowered before saving.
  std::wstring lowered;
  for (int i : st::enforce_needs(g.st.values))
    lowered += (lowered.empty() ? L"" : L", ") + widen(st::kKnobs[i].label);
  std::wstring err;
  if (!save_state(g.st, &err)) {
    status(L"Save failed: " + err);
    return false;
  }
  if (!quiet)
    status(L"Saved " + user_ini_path() +
           (game_running() ? L". The running game applies it within a second (restart-only settings at the next start)."
                           : L".") +
           (lowered.empty() ? L"" : L" Lowered (a required setting is off): " + lowered + L"."));
  else if (!lowered.empty())
    status(L"Lowered (a required setting is off): " + lowered + L".");
  refresh();
  return true;
}

static void save_and_launch() {
  std::vector<int> before = g.st.loaded;
  if (!save(true)) return;
  std::wstring dir = game_dir_text();
  if (!valid_game_dir(dir)) {
    status(L"Settings saved. Set the game folder (the one that contains bin\\bg3.exe) to launch.");
    return;
  }
  save_game_dir(dir);
  if (game_running()) {
    status(L"Settings saved. The game is already running: they apply within a second (restart-only ones at the next "
           L"start).");
    return;
  }
  LaunchPlan p = make_plan(dir, L"", {}, IsDlgButtonChecked(g.wnd, IDC_CLEAN) == BST_CHECKED, effective_fg(g.st));
  DWORD pid = 0;
  std::wstring err;
  if (launch(p, &pid, &err)) status(L"Settings saved. Started " + p.exe + L" (pid " + std::to_wstring(pid) + L").");
  else status(L"Settings saved. Launch failed: " + err);
}

static void on_choice(int i, int c) {
  const st::Knob& k = st::kKnobs[i];
  if (!st::choice_supported(k, c, g.st.caps)) {
    status(widen(k.label) + L": " + widen(k.choices[c].label) + L" is not supported on this GPU.");
    refresh();
    return;
  }
  g.st.values[i] = c;
  std::string need = st::unmet_need(k, c, g.st.values);
  if(need.empty())need=st::unmet_key_need(k,c,effective_settings());
  status(need.empty() ? L"" : widen(k.label) + L": " + widen(need) + L".");
  refresh();
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_SIZE: scroll_to(g.scroll_y);break;
    case WM_VSCROLL: {
      SCROLLINFO si{sizeof si,SIF_ALL};GetScrollInfo(h,SB_VERT,&si);
      int target=g.scroll_y;
      switch(LOWORD(wp)){
        case SB_TOP:target=0;break;case SB_BOTTOM:target=g.content_height;break;
        case SB_LINEUP:target-=S(30);break;case SB_LINEDOWN:target+=S(30);break;
        case SB_PAGEUP:target-=int(si.nPage);break;case SB_PAGEDOWN:target+=int(si.nPage);break;
        case SB_THUMBTRACK:target=si.nTrackPos;break;
      }
      scroll_to(target);return 0;
    }
    case WM_MOUSEWHEEL:scroll_to(g.scroll_y-GET_WHEEL_DELTA_WPARAM(wp)*S(90)/WHEEL_DELTA);return 0;
    case WM_COMMAND: {
      int id = LOWORD(wp), code = HIWORD(wp);
      if (g.updating) break;
      if (id == IDC_PRESET && code == CBN_SELCHANGE) {
        int p = int(SendMessageW(item(IDC_PRESET), CB_GETCURSEL, 0, 0));
        if (p >= 0) {
          g.st.preset = p;
          g.st.values = st::preset_values_for(p, g.st.caps);
          g.desc_knob = -2;
          show_desc(-1);
          refresh();
        }
      } else if (id == IDC_PRESET && code == CBN_SETFOCUS) {
        g.desc_knob = -2;
        show_desc(-1);
      } else if (id >= IDC_KNOB && id < IDC_KNOB + st::kKnobCount) {
        int i = id - IDC_KNOB;
        if (code == CBN_SETFOCUS) show_desc(i);
        if (code == CBN_SELCHANGE) {
          show_desc(i);
          int c = int(SendMessageW(item(id), CB_GETCURSEL, 0, 0));
          if (c >= 0) on_choice(i, c);
        }
      } else if (code == BN_CLICKED) {
        if (id == IDC_SUGGEST) {
          g.st.preset = g.st.suggested;
          g.st.values = st::preset_values_for(g.st.preset, g.st.caps);
          g.desc_knob = -2;
          show_desc(-1);
          refresh();
        } else if (id == IDC_RESET) {
          g.st.values = st::preset_values_for(g.st.preset, g.st.caps);
          refresh();
          status(L"Reset to " + widen(st::kPresetLabel[g.st.preset]) + L" (not saved yet).");
        } else if (id == IDC_SAVE) {
          save(false);
        } else if (id == IDC_LAUNCH) {
          save_and_launch();
        } else if (id == IDC_CLOSE) {
          DestroyWindow(h);
        } else if (id == IDC_BROWSE) {
          browse();
        }
      }
      break;
    }
    case WM_HSCROLL: {
      HWND t = reinterpret_cast<HWND>(lp);
      int id = GetDlgCtrlID(t);
      if (g.updating || id < IDC_KNOB || id >= IDC_KNOB + st::kKnobCount) break;
      int i = id - IDC_KNOB;
      const st::Knob& k = st::kKnobs[i];
      g.st.values[i] = slider_from_pos(k, int(SendMessageW(t, TBM_GETPOS, 0, 0)));
      show_desc(i);
      refresh();
      break;
    }
    case WM_CTLCOLORSTATIC: {
      HDC dc = reinterpret_cast<HDC>(wp);
      int id = GetDlgCtrlID(reinterpret_cast<HWND>(lp));
      SetBkMode(dc, TRANSPARENT);
      if (id >= IDC_BADGE && id < IDC_BADGE + st::kKnobCount) SetTextColor(dc, g.badge_color[id - IDC_BADGE]);
      else if (id == IDC_PRESET_NOTE || id == IDC_DESC) SetTextColor(dc, RGB(80, 80, 80));
      return LRESULT(g.bg);
    }
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

// --screenshot <file.bmp>: renders the window off screen, without activating
// it, into a bitmap and exits (layout checks without disturbing the desktop).
static void screenshot(const std::wstring& path) {
  RECT r;
  GetWindowRect(g.wnd, &r);
  int w = r.right - r.left, h = r.bottom - r.top;
  HDC screen = GetDC(nullptr);
  HDC dc = CreateCompatibleDC(screen);
  BITMAPINFO bi{};
  bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
  void* bits = nullptr;
  HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  HGDIOBJ old = SelectObject(dc, bmp);
  PrintWindow(g.wnd, dc, PW_RENDERFULLCONTENT);
  GdiFlush();
  BITMAPFILEHEADER fh{0x4D42, DWORD(sizeof fh + sizeof bi.bmiHeader + size_t(w) * h * 4), 0, 0,
                      DWORD(sizeof fh + sizeof bi.bmiHeader)};
  std::string data(reinterpret_cast<char*>(&fh), sizeof fh);
  data.append(reinterpret_cast<char*>(&bi.bmiHeader), sizeof bi.bmiHeader);
  data.append(static_cast<char*>(bits), size_t(w) * h * 4);
  write_file_atomic(path, data, nullptr);
  SelectObject(dc, old);
  DeleteObject(bmp);
  DeleteDC(dc);
  ReleaseDC(nullptr, screen);
}

static std::wstring g_screenshot;
static bool g_screenshot_bottom=false;
static float g_screenshot_scale=0;

static int run_window(HINSTANCE inst, const std::wstring& game_dir_arg) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  INITCOMMONCONTROLSEX icc{sizeof icc, ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
  InitCommonControlsEx(&icc);
  const float native_scale=float(GetDpiForSystem())/96.0f;
  g.s = g_screenshot_scale>0?g_screenshot_scale:native_scale;
  NONCLIENTMETRICSW ncm{sizeof ncm};
  SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
  if(g_screenshot_scale>0)ncm.lfMessageFont.lfHeight=LONG(ncm.lfMessageFont.lfHeight*g.s/native_scale);
  g.font = CreateFontIndirectW(&ncm.lfMessageFont);
  LOGFONTW b = ncm.lfMessageFont;
  b.lfWeight = FW_SEMIBOLD;
  g.bold = CreateFontIndirectW(&b);
  g.bg = GetSysColorBrush(COLOR_WINDOW);

  load_state(g.st);
  detect_gpu(g.st, &g.gpu_err);
  g.dev = st::dev_mode(st::lookup(saved_settings(), "dev"));

  WNDCLASSEXW wc{sizeof wc};
  wc.lpfnWndProc = wndproc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = g.bg;
  wc.lpszClassName = L"BG3RTXLauncher";
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  RegisterClassExW(&wc);
  g.wnd = CreateWindowExW(0, wc.lpszClassName, L"BG3RTX settings", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VSCROLL,
                          CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, nullptr, nullptr, inst, nullptr);
  build_window();

  std::wstring dir = !game_dir_arg.empty() ? game_dir_arg : saved_game_dir();
  if (!valid_game_dir(dir)) dir = detect_game_dir();
  SetWindowTextW(item(IDC_GAMEDIR), dir.c_str());
  show_desc(-1);
  refresh();
  status(file_exists(user_ini_path()) ? L"Loaded " + user_ini_path()
                                      : L"No bg3rtx_user.ini yet: showing the defaults (High). Pick a preset and Save.");
  if (!g_screenshot.empty()) {
    SetWindowPos(g.wnd, nullptr, -20000, -20000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(g.wnd, SW_SHOWNOACTIVATE);
    if(g_screenshot_bottom)scroll_to(g.content_height);
    RedrawWindow(g.wnd,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
    screenshot(g_screenshot);
    DestroyWindow(g.wnd);
    return 0;
  }
  ShowWindow(g.wnd, SW_SHOW);

  MSG m;
  while (GetMessageW(&m, nullptr, 0, 0) > 0) {
    if (IsDialogMessageW(g.wnd, &m)) continue;
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
  CoUninitialize();
  return 0;
}

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
  bool do_launch = false, dry = false, detect = false, dump = false, clean = false, clamp = true;
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
    else if (a == L"--dump-schema") dump = true;
    else if (a == L"--clean") clean = true;
    else if (a == L"--no-clamp") clamp = false;
    else if (a == L"--game") game_dir = next();
    else if (a == L"--exe") exe = next();
    else if (a == L"--preset") preset = next();
    else if (a == L"--out") g_out_file = next();
    else if (a == L"--screenshot") g_screenshot = next();
    else if (a == L"--screenshot-bottom") g_screenshot_bottom=true;
    else if (a == L"--screenshot-scale") g_screenshot_scale=float(_wtof(next().c_str()));
    else if (a == L"--help" || a == L"-h" || a == L"/?") {
      emit(L"BG3RTX.exe [--launch [--dry-run]] [--preset low|medium|high|ultra [--no-clamp]] [--detect]\n"
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
  if((g_screenshot_bottom || g_screenshot_scale!=0) && (g_screenshot.empty() || do_launch || detect || dump || !preset.empty() || g_screenshot_scale<0 || g_screenshot_scale>3))return 2;

  if (!do_launch && !detect && !dump && preset.empty()) return run_window(inst, game_dir);

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
        if (launch(p, &pid, &err)) emit(L"started " + p.exe + L" (pid " + std::to_wstring(pid) + L")\n");
        else emit(L"error: " + err + L"\n"), rc = 1;
      }
    }
  }
  flush_output();
  return rc;
}
