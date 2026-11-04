// Native launcher presentation. Included by main.cpp; no extra build registration.
#include <tlhelp32.h>
#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")
#include "ui_state.h"
#include "version.h"
#include "compatibility.h"
#include "../../common/experimental_ab.h"

static std::wstring g_screenshot, g_review;
static std::vector<int> g_tab_ids;
static GraphicsInspection g_inspection;
static int g_adapter=-1;
static bool g_screenshot_bottom=false, g_no_game=false;
static float g_screenshot_scale=0;
static int g_review_width=0;
enum { PRESET=20, SUGGEST, TABS, EXPAND, SAVE, PLAY, DISCARD, CLOSE, REPORT, BROWSE,
       GAME, CLEAN, STATUS, NOTE, NEXT, RETRY, LOG, FOLDER, RESET, ERROR_DETAILS, ADAPTER, PRESET_LOW=60, KNOB=1000, DETAILS=2000 };
struct Ui {
  HWND wnd{}, body{}; HFONT font{}, bold{}; HBRUSH bg{};
  bg3::experimental_ab::Selection ab_selection;
  State st; st::Map baseline; std::vector<int> saved;
  int saved_preset{}, tab=0, scroll=0, height=0, stage=0;
  float scale=1; bool dev=false, experimental=false, updating=false, pending=false, high_contrast=false;
  std::wstring dir, saved_dir, gpu_err, message; std::string session; bool clean=false;
} g;
static int S(int n) { return int(n*g.scale+0.5f); }
static COLORREF background() { return g.high_contrast?GetSysColor(COLOR_WINDOW):RGB(23,25,29); }
static COLORREF foreground() { return g.high_contrast?GetSysColor(COLOR_WINDOWTEXT):RGB(241,231,210); }
static HWND control(HWND parent,int id) { return GetDlgItem(parent,id); }
static LRESULT CALLBACK focusproc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR);
static HWND add(HWND parent,const wchar_t* cls,const std::wstring& text,DWORD style,int id,bool bold=false) {
  HWND h=CreateWindowExW(0,cls,text.c_str(),WS_CHILD|WS_VISIBLE|style,0,0,0,0,parent,(HMENU)INT_PTR(id),GetModuleHandleW(nullptr),nullptr);
  SendMessageW(h,WM_SETFONT,WPARAM(bold?g.bold:g.font),TRUE); if(std::wstring(cls)==WC_COMBOBOXW)SendMessageW(h,CB_SETITEMHEIGHT,WPARAM(-1),S(28)); if(style&WS_TABSTOP)SetWindowSubclass(h,focusproc,1,0); return h;
}
static void place(HWND h,int x,int y,int w,int height) { MoveWindow(h,S(x),S(y),S(std::max(1,w)),S(height),TRUE); }
static void label(HWND p,const std::wstring& text,int x,int y,int w,int h,bool bold=false) {place(add(p,L"STATIC",text,SS_LEFT,0,bold),x,y,w,h);}
static void button(HWND p,int id,const std::wstring& text,int x,int y,int w) {place(add(p,L"BUTTON",text,WS_TABSTOP|BS_PUSHBUTTON,id),x,y,w,32);}
static st::Map saved_settings() { st::Map m; st::apply_layer(m,g.st.base);st::apply_layer(m,g.st.user);return m; }
static st::Map effective_settings() {
  auto m=saved_settings();for(int i=0;i<st::kKnobCount;++i){st::KVList v;st::knob_kvs(st::kKnobs[i],g.st.values[i],v);for(auto& kv:v)m[kv.first]=kv.second;}return m;
}
static int dirty_count() {return std::max(int(g.ab_selection.pending),launcher::ui_state::dirty(g.st.values,g.saved,g.st.preset,g.saved_preset,g.dir,g.saved_dir));}
static bool needs_restart(const st::Map& a,const st::Map& b) {return launcher::ui_state::restart(a,b);}
static std::string game_session() {
  HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)return {};
  PROCESSENTRY32W pe{sizeof pe};std::string token;
  if(Process32FirstW(snapshot,&pe))do {
    if(_wcsicmp(pe.szExeFile,L"bg3.exe")!=0)continue;
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pe.th32ProcessID);
    FILETIME start{},end{},kernel{},user{};
    if(process){if(GetProcessTimes(process,&start,&end,&kernel,&user))token=std::to_string(pe.th32ProcessID)+":"+std::to_string(start.dwHighDateTime)+":"+std::to_string(start.dwLowDateTime);CloseHandle(process);}
    break;
  }while(Process32NextW(snapshot,&pe));CloseHandle(snapshot);return token;
}
static std::wstring ui_path() {return root_dir()+L"\\bg3rtx_ui.ini";}
static bool persist(std::wstring* err=nullptr) {
  if(!g_screenshot.empty()||g_no_game)return true;
  std::string t=launcher::ui_state::serialize(g.stage,g.pending,g.session,g.baseline);
  t+="onboarding_game="+narrow(g.dir)+"\n";
  return write_file_atomic(ui_path(),t,err);
}
static void footer() {
  std::wstring t=std::to_wstring(dirty_count())+L" unsaved changes. ";
  bool running=game_running();
  int pending=(g.pending&&(running||!g_screenshot.empty()))?launcher::ui_state::restart_count(g.baseline,saved_settings()):0;
  t+=pending?widen(launcher::ui_state::restart_banner(pending))+L". ":L"Saved for next launch. ";
  if(running)t+=L"Game is running. ";
  if(!g.message.empty())t+=L"\r\n"+(g.message.size()>220?g.message.substr(0,220)+L"... (Details / report)":g.message);
  ShowWindow(control(g.wnd,ERROR_DETAILS),g.message.empty()?SW_HIDE:SW_SHOW);
  SetWindowTextW(control(g.wnd,STATUS),t.c_str());
  auto preset=st::preset_values_for(g.st.preset,g.st.caps);int custom=0;
  for(int i=0;i<st::kKnobCount;++i)if(st::knob_visible(st::kKnobs[i],g.dev))custom+=g.st.values[i]!=preset[i];
  std::wstring note=!g.st.have_gpu?L"Compatibility: Unknown\nRecheck in Overview":custom?L"Custom: "+std::to_wstring(custom)+L" settings\nSuitability: estimate":L"Matches preset\nSuitability: estimate";
  SetWindowTextW(control(g.wnd,NOTE),note.c_str());
  for(int p=0;p<st::kPresetCount;++p)SendMessageW(control(g.wnd,PRESET_LOW+p),BM_SETCHECK,!custom&&p==g.st.preset?BST_CHECKED:BST_UNCHECKED,0);
  EnableWindow(control(g.wnd,PLAY),!g_no_game&&g_screenshot.empty()&&g.stage>=4&&valid_game_dir(g.dir)&&!running);
}
static void status(const std::wstring& s) {g.message=s;footer();}
static int body_width() { RECT r{};GetClientRect(g.body,&r);return int(r.right/g.scale)-32; }
static void scroll_to(int n) {
  RECT r{};GetClientRect(g.body,&r);int next=std::clamp(n,0,std::max(0,g.height-int(r.bottom)));
  if(next!=g.scroll){ScrollWindowEx(g.body,0,g.scroll-next,nullptr,nullptr,nullptr,nullptr,SW_SCROLLCHILDREN|SW_INVALIDATE|SW_ERASE);g.scroll=next;}
  SCROLLINFO si{sizeof si,SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,g.height-1),UINT(r.bottom),g.scroll};SetScrollInfo(g.body,SB_VERT,&si,TRUE);
}
static LRESULT CALLBACK focusproc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR) {
  if(m==WM_SETFOCUS && GetParent(h)==g.body){
    RECT child{},body{};GetWindowRect(h,&child);GetWindowRect(g.body,&body);
    if(child.top<body.top)scroll_to(g.scroll+child.top-body.top-S(16));
    else if(child.bottom>body.bottom)scroll_to(g.scroll+child.bottom-body.bottom+S(16));
  }
  return DefSubclassProc(h,m,w,l);
}
static void rebuild();
static void layout() {
  if(!g.body)return;RECT r{};GetClientRect(g.wnd,&r);int w=int(r.right/g.scale),h=int(r.bottom/g.scale);
  for(int p=0;p<st::kPresetCount;++p)place(control(g.wnd,PRESET_LOW+p),96+p*72,56,68,32);place(control(g.wnd,SUGGEST),392,56,176,32);
  place(control(g.wnd,NOTE),584,56,w-600,40);place(control(g.wnd,TABS),16,104,w-32,32);
  place(g.body,0,144,w,h-296);place(control(g.wnd,STATUS),16,h-144,w-32,80);place(control(g.wnd,ERROR_DETAILS),16,h-48,160,32);
  int x=w-16;for(auto pair:{std::pair<int,int>{CLOSE,80},{PLAY,96},{SAVE,80},{DISCARD,88},{RESET,144}}){x-=pair.second;place(control(g.wnd,pair.first),x,h-48,pair.second,32);x-=8;}
  rebuild();
}
static bool matches(const st::Knob& k) {
  if(!st::knob_visible(k,g.dev))return false;
  if(k.dev_only)return g.tab==5;
  if(k.experimental)return g.tab==3&&g.experimental;
  std::string c=k.category;
  return (g.tab==1&&c=="Lighting")||(g.tab==6&&c=="Surfaces")||(g.tab==2&&c=="Performance")||(g.tab==3&&c=="Advanced");
}
static std::wstring choice_label(const st::Knob& k,int c) {
  if(c<0)return L"Custom value preserved";
  std::wstring s=widen(k.choices[c].label);
  std::string maturity=st::choice_maturity(k,c);
  if(maturity=="awaiting_qualification")s+=L" (awaiting qualification)";
  if(maturity=="experimental")s+=L" (experimental)";
  if(!st::choice_supported(k,c,g.st.caps))s+=L" (unavailable)";
  return s;
}
static void rebuild() {
  if(!g.body)return;HWND focused=GetFocus();int focus_id=GetParent(focused)==g.body?GetDlgCtrlID(focused):0;int previous_scroll=g.scroll;g.updating=true;g.scroll=0;
  while(HWND h=GetWindow(g.body,GW_CHILD))DestroyWindow(h);
  int w=body_width(), y=16;
  if(g.tab==0) {
    static const wchar_t* steps[]={L"Welcome to BG3RTX",L"Choose your game",L"Review graphics compatibility",L"Ready for your first launch"};
    label(g.body,g.stage<4?steps[g.stage]:L"Overview",16,y,w,32,true);y+=40;
    std::wstring intro=g.stage==0?L"Independent fan mod. Review your existing settings before making changes. Nothing starts automatically.":
      g.stage==1?L"Choose the folder containing bin\\bg3.exe (Vulkan). You can finish setup without a game folder.":
      g.stage==2?L"Review the recommended preset. Hardware suitability is an estimate, not a frame-rate promise.":
      L"In the game's Graphics options, select DLSS (Quality recommended) for Ray Reconstruction. First Play is always your choice.";
    label(g.body,intro,16,y,w,64);y+=72;
    label(g.body,gpu_line(g.st,g.gpu_err),16,y,w,72);y+=80;
    button(g.body,RETRY,L"Recheck graphics",16,y,176);y+=48;
    label(g.body,L"Game folder (Vulkan)",16,y,w,24,true);y+=32;
    place(add(g.body,L"EDIT",g.dir,WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,GAME),16,y,w-136,32);
    button(g.body,BROWSE,L"Browse...",w-104,y,120);y+=48;
    label(g.body,valid_game_dir(g.dir)?L"Game found. Play saves your settings first.":L"Game missing. Choose the folder containing bin\\bg3.exe. Settings remain available.",16,y,w,56);y+=64;
    label(g.body,widen(st::kPresetDesc[g.st.preset]),16,y,w,96);y+=104;
    if(g.stage<4){button(g.body,NEXT,g.stage==3?L"Finish setup":L"Continue setup",16,y,176);y+=48;}
  } else if(g.tab==4) {
    label(g.body,L"Help & support",16,y,w,32,true);y+=40;
    label(g.body,L"Create a local report when the report service is available. Nothing is uploaded. Review its contents before sharing. Saves and crash dumps are excluded by default.",16,y,w,72);y+=80;
    button(g.body,REPORT,L"Report a problem",16,y,200);y+=48;
    button(g.body,BROWSE,L"Choose game folder...",16,y,224);y+=48;
    button(g.body,RETRY,L"Recheck graphics",16,y,224);y+=48;
    label(g.body,L"Overlay conflicts: try disabling Steam, EOS or GOG overlays for one launch. No driver minimum or VRAM budget has been verified here. Restore previous version is unavailable until setup provides a verified backup.",16,y,w,96);y+=104;
    const auto compatibility=assess_compatibility(g_inspection,g_adapter);
    label(g.body,widen(compatibility.hardware_text+"\n"+compatibility.driver_text+"\n"+compatibility.memory_text+"\n"+compatibility.adapter_text),16,y,w,144);y+=152;
    if(g_inspection.adapters.size()>1){
      label(g.body,L"Graphics adapter",16,y,w,28,true);y+=36;
      HWND adapter=add(g.body,WC_COMBOBOXW,L"",WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST,ADAPTER);place(adapter,16,y,w,240);
      SendMessageW(adapter,CB_ADDSTRING,0,LPARAM(L"Choose adapter"));
      for(const auto& a:g_inspection.adapters)SendMessageW(adapter,CB_ADDSTRING,0,LPARAM(widen(a.gpu.name).c_str()));
      SendMessageW(adapter,CB_SETCURSEL,g_adapter+1,0);y+=48;
    }
    label(g.body,L"Ctrl+Shift+F9 compares BG3RTX with the game's own image.",16,y,w,48);y+=56;
    label(g.body,L"Fonts: Lato Web, SIL Open Font License. Credits and notices: docs/release/THIRD_PARTY.md.",16,y,w,64);y+=72;
  } else {
    if(g.tab==3&&g.dev) {
      int enabled=0;for(int i=0;i<st::kKnobCount;++i)if(g.st.values[i]>0&&std::string(st::choice_maturity(st::kKnobs[i],g.st.values[i]))=="experimental")++enabled;
      button(g.body,EXPAND,(g.experimental?L"Hide Experimental":L"Show Experimental")+std::wstring(L" (")+std::to_wstring(enabled)+L" enabled)",16,y,w);y+=48;
    }
    std::string category;
    for(int i=0;i<st::kKnobCount;++i) {
      const auto& k=st::kKnobs[i];if(!matches(k))continue;
      if(category!=k.category){category=k.category;label(g.body,widen(category),16,y,w,28,true);y+=40;}
      label(g.body,widen(k.label),32,y,w-32,28,true);y+=36;
      HWND c;
      if(k.kind==st::Kind::Choice){
        c=add(g.body,WC_COMBOBOXW,L"",WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST,KNOB+i);place(c,32,y,w-32,240);
        auto append=[&](int v){int row=int(SendMessageW(c,CB_ADDSTRING,0,LPARAM(choice_label(k,v).c_str())));SendMessageW(c,CB_SETITEMDATA,row,v);if(v==g.st.values[i])SendMessageW(c,CB_SETCURSEL,row,0);};
        if(g.st.values[i]<0)append(-1);
        for(int ch=0;ch<st::choice_count(k);++ch)if(st::choice_visible(k,ch,g.dev)&&(g.experimental||!st::choice_requires_experimental(k,ch)))append(ch);
        if(g.st.values[i]>=0&&SendMessageW(c,CB_GETCURSEL,0,0)==CB_ERR){int row=int(SendMessageW(c,CB_ADDSTRING,0,LPARAM(L"Custom value preserved")));SendMessageW(c,CB_SETITEMDATA,row,-1);SendMessageW(c,CB_SETCURSEL,row,0);}
      } else {
        c=add(g.body,TRACKBAR_CLASSW,L"",WS_TABSTOP|TBS_HORZ|TBS_NOTICKS,KNOB+i);place(c,32,y,w-160,32);
        SendMessageW(c,TBM_SETRANGE,TRUE,MAKELPARAM(k.slider.has_off?k.slider.min-1:k.slider.min,k.slider.max));SendMessageW(c,TBM_SETPOS,TRUE,g.st.values[i]<0?k.slider.max:(k.slider.has_off&&g.st.values[i]==0?k.slider.min-1:g.st.values[i]));
        SetWindowTextW(c,widen(k.label).c_str());label(g.body,g.st.values[i]<0?L"Custom":g.st.values[i]==0?L"Off":std::to_wstring(g.st.values[i])+L" "+widen(k.slider.unit),w-112,y,112,32);
      }
      y+=48;label(g.body,widen(k.desc),32,y,w-32,72);y+=80;
      std::wstring badge=L"GPU cost: "+(std::string(k.cost.gpu)=="Unknown"||std::string(k.cost.gpu)=="Not measured"?std::wstring(L"Not measured"):widen(k.cost.gpu))+L" | VRAM: "+(std::string(k.cost.vram)=="Unknown"?std::wstring(L"Unknown"):widen(k.cost.status)+L" "+widen(k.cost.vram));
      label(g.body,badge,32,y,w-32,48);y+=56;
      auto need=st::unmet_need(k,g.st.values[i],g.st.values);if(need.empty())need=st::unmet_key_need(k,g.st.values[i],effective_settings());
      label(g.body,widen(st::apply_label(st::transition_apply(k,i<int(g.saved.size())?g.saved[i]:-1,g.st.values[i]))),32,y,w-32,28,true);y+=36;
      for(int ch=0;ch<st::choice_count(k);++ch)if(st::choice_visible(k,ch,g.dev)){
        for(const auto& n:k.choices[ch].need)if(n.knob){auto j=st::knob_index(n.knob);if(j>=0){if(!need.empty())need+="\n";need+=std::string(k.choices[ch].label)+": requires "+st::kKnobs[j].label+" "+st::kKnobs[j].choices[n.min_choice].label;}}
      }
      for(const auto& n:k.required_on)if(n.key){if(!need.empty())need+="\n";need+="Requires "+std::string(n.label);}
      if(!need.empty()){int lines=1+int(std::count(need.begin(),need.end(),'\n'));int height=24*lines+24*int(need.size()/std::max(30,w/9));label(g.body,widen(need),32,y,w-32,height);y+=height+8;}
      button(g.body,DETAILS+i,L"Details and requirements",32,y,256);y+=48;place(add(g.body,L"STATIC",L"",SS_ETCHEDHORZ,0),16,y,w,2);y+=24;
    }
  }
  g.height=S(y+16);scroll_to(previous_scroll);g.updating=false;footer();if(focus_id)if(HWND target=control(g.body,focus_id))SetFocus(target);
}
static void browse() {
  IFileOpenDialog* d=nullptr;if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&d))))return;
  DWORD opts=0;d->GetOptions(&opts);d->SetOptions(opts|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);d->SetTitle(L"Choose game folder containing bin\\bg3.exe");
  if(SUCCEEDED(d->Show(g.wnd))){IShellItem* it=nullptr;if(SUCCEEDED(d->GetResult(&it))){PWSTR p=nullptr;if(SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH,&p))){auto dir=game_dir_from_path(p);g.dir=dir.empty()?p:dir;CoTaskMemFree(p);rebuild();}it->Release();}}d->Release();
}
static void report_problem() { launcher::web::native_report(g.wnd); }
static bool save(bool) {
  auto preview=st::preview_dependencies(g.st.values,effective_settings(),g.st.caps,false,g.dev,g.experimental);
  if(!preview.changed.empty()){
    int policy=MessageBoxW(g.wnd,L"Some selections need other features.\nYes: Enable required settings\nNo: Lower dependent settings\nCancel: Keep editing\n\nYou will review every adjustment before saving.",L"Resolve requirements",MB_YESNOCANCEL|MB_ICONQUESTION);
    if(policy==IDCANCEL)return false;
    if(policy==IDYES)preview=st::preview_dependencies(g.st.values,effective_settings(),g.st.caps,true,g.dev,g.experimental);
  }
  bool adjustments=!preview.changed.empty()||!preview.required_keys.empty();
  if(adjustments){
    std::wstring text=L"Review adjustments before saving:\n";
    for(int i:preview.changed)text+=L"\n"+widen(st::kKnobs[i].label)+L": "+choice_label(st::kKnobs[i],g.st.values[i])+L" -> "+choice_label(st::kKnobs[i],preview.values[i]);
    for(auto& kv:preview.required_keys){
      std::wstring label=L"Startup prerequisite";
      for(const auto& k:st::kKnobs)for(const auto& n:k.required_on)if(n.key&&kv.first==n.key)label=widen(n.label);
      text+=L"\n"+label+L": enable at next launch";
    }
    text+=L"\n\nOK applies these adjustments. Cancel keeps your edits.";
    if(MessageBoxW(g.wnd,text.c_str(),L"Review dependent settings",MB_OKCANCEL|MB_ICONWARNING)!=IDOK)return false;
  }
  if(!preview.blocked.empty()){std::wstring t=L"Settings could not be saved. Review:\n";for(auto& b:preview.blocked)t+=L"\n"+widen(b);MessageBoxW(g.wnd,t.c_str(),L"Review requirements",MB_OK|MB_ICONWARNING);return false;}
  State candidate=g.st;candidate.values=preview.values;
  auto forced=forced_keys(candidate);forced.insert(forced.end(),preview.required_keys.begin(),preview.required_keys.end());
  g.ab_selection.append(forced);
  auto text=st::render_user_ini(candidate.user_text,candidate.preset,candidate.values,forced);
  auto after=st::parse_ini(text);st::Map effective;st::apply_layer(effective,candidate.base);st::apply_layer(effective,after);
  bool old_pending=g.pending;g.pending=game_running()&&needs_restart(g.baseline,effective);
  std::wstring err;
  // Persist the warning before the settings transaction; a failed settings write may leave a conservative warning.
  if(!persist(&err)){g.pending=old_pending;status(L"Settings not saved: restart state could not be stored. "+err);return false;}
  if(!write_file_atomic(user_ini_path(),text,&err)){status(L"Settings could not be saved. Your edits are kept. "+err);return false;}
  g.ab_selection.pending=false;
  g.st.user_text=text;g.st.user=after;g.st.values=preview.values;g.st.loaded=preview.values;
  save_game_dir(g.dir);g.saved=g.st.values;g.saved_preset=g.st.preset;g.saved_dir=g.dir;
  status(g.pending?L"Saved; restart required. The game will not be restarted automatically.":L"Saved. Live settings apply automatically; Ctrl+Shift+F9 compares BG3RTX with the game's own image.");rebuild();return true;
}
static void select_preset(int p) {
  auto v=st::preset_values_for(p,g.st.caps);
  // Preset review replaces visible custom selections; hidden controls stay intact.
  for(int i=0;i<st::kKnobCount;++i)if(!st::knob_visible(st::kKnobs[i],g.dev))v[i]=g.st.values[i];
  std::wstring t=L"Preview preset: "+widen(st::kPresetLabel[p])+L"\n";
  for(int i=0;i<st::kKnobCount;++i)if(v[i]!=g.st.values[i])t+=L"\n"+widen(st::kKnobs[i].label)+L": "+(st::kKnobs[i].kind==st::Kind::Choice?choice_label(st::kKnobs[i],v[i]):std::to_wstring(v[i]));
  if(MessageBoxW(g.wnd,t.c_str(),L"Replace settings with preset?",MB_OKCANCEL|MB_ICONQUESTION)==IDOK){g.st.preset=p;g.st.values=v;rebuild();}
  footer();
}
static void play() {
  if(g_no_game||!g_screenshot.empty()||game_running()||g.stage<4)return;
  if(!save(false)||!valid_game_dir(g.dir))return;
  auto p=make_plan(g.dir,L"",{},g.clean,effective_fg(g.st));DWORD pid=0;std::wstring err;
  if(!launch(p,&pid,&err))status(L"Settings saved; game could not start. "+err+L" Use Support > Report a problem.");
  else status(L"Game started. Saved settings have no runtime acknowledgement.");
}
static void command(int id,int code,HWND source) {
  if(g.updating)return;
  if(id>=KNOB&&id<KNOB+st::kKnobCount&&code==CBN_SELCHANGE){
    int i=id-KNOB;const auto& k=st::kKnobs[i];int row=int(SendMessageW(source,CB_GETCURSEL,0,0));int c=int(SendMessageW(source,CB_GETITEMDATA,row,0));
    if(c<0)return;
    if(!st::choice_available(k,c,g.dev,g.experimental,g.st.caps)){status(L"This choice is unavailable. Check graphics compatibility and requirements.");int previous=g.scroll;rebuild();scroll_to(previous);SetFocus(control(g.body,KNOB+i));return;}
    g.ab_selection.touch(k,c);g.st.values[i]=c;int previous=g.scroll;rebuild();scroll_to(previous);SetFocus(control(g.body,KNOB+i));return;
  }
  if(id>=DETAILS&&id<DETAILS+st::kKnobCount){auto& k=st::kKnobs[id-DETAILS];std::wstring t=widen(k.desc)+L"\n\n"+widen(k.details)+L"\n\n"+widen(k.cost.provenance)+L"\n\nApply: "+widen(st::apply_label(st::transition_apply(k,id-DETAILS<int(g.saved.size())?g.saved[id-DETAILS]:-1,g.st.values[id-DETAILS])))+L"\n"+widen(k.apply_note);MessageBoxW(g.wnd,t.c_str(),widen(k.label).c_str(),MB_OK);return;}
  if(id==ADAPTER&&code==CBN_SELCHANGE){
    g_adapter=int(SendMessageW(source,CB_GETCURSEL,0,0))-1;
    auto c=assess_compatibility(g_inspection,g_adapter);g.st.caps=c.caps;g.st.suggested=c.model_preset;g.st.have_gpu=g_adapter>=0;
    if(g.st.have_gpu)g.st.gpu=g_inspection.adapters[g_adapter].gpu;rebuild();return;
  }
  if(id==GAME&&code==EN_CHANGE){int n=GetWindowTextLengthW(source);std::wstring t(n+1,L'\0');GetWindowTextW(source,t.data(),n+1);t.resize(n);g.dir=t;footer();return;}
  if(id>=PRESET_LOW&&id<PRESET_LOW+st::kPresetCount&&code==BN_CLICKED){select_preset(id-PRESET_LOW);footer();return;}
  if(code!=BN_CLICKED)return;
  switch(id){
    case SUGGEST:select_preset(g.st.suggested);break;
    case EXPAND:g.experimental=!g.experimental;rebuild();SetFocus(control(g.body,EXPAND));break;
    case NEXT:{int previous=g.stage;g.stage=std::min(4,g.stage+1);std::wstring err;if(!persist(&err)){g.stage=previous;status(L"Setup progress could not be saved. "+err);}else rebuild();break;}
    case SAVE:save(false);break;case PLAY:play();break;
    case DISCARD:g.ab_selection.pending=false;g.st.values=g.saved;g.st.preset=g.saved_preset;g.dir=g.saved_dir;rebuild();break;
    case RESET:select_preset(g.st.preset);break;
    case CLOSE:SendMessageW(g.wnd,WM_CLOSE,0,0);break;
    case BROWSE:browse();break;
    case REPORT:report_problem();break;
    case ERROR_DETAILS:if(MessageBoxW(g.wnd,(g.message+L"\n\nOpen Report a problem?").c_str(),L"Status details",MB_YESNO|MB_ICONINFORMATION)==IDYES)report_problem();break;
    case RETRY:g.st.have_gpu=false;g.st.caps=0;g.gpu_err.clear();detect_gpu(g.st,&g.gpu_err);g_inspection=inspect_graphics();g_adapter=g_inspection.adapters.size()==1?0:-1;g.st.caps=assess_compatibility(g_inspection,g_adapter).caps;rebuild();break;
    case CLEAN:g.clean=SendMessageW(source,BM_GETCHECK,0,0)==BST_CHECKED;break;
    case LOG:ShellExecuteW(g.wnd,L"open",(root_dir()+L"\\bg3rtx.log").c_str(),nullptr,nullptr,SW_SHOWNORMAL);break;
    case FOLDER:ShellExecuteW(g.wnd,L"open",root_dir().c_str(),nullptr,nullptr,SW_SHOWNORMAL);break;
  }
}
static LRESULT CALLBACK bodyproc(HWND h,UINT m,WPARAM w,LPARAM l) {
  switch(m){
    case WM_COMMAND:command(LOWORD(w),HIWORD(w),(HWND)l);return 0;
    case WM_HSCROLL:{HWND c=(HWND)l;int i=GetDlgCtrlID(c)-KNOB;if(i>=0&&i<st::kKnobCount){auto& k=st::kKnobs[i];int p=int(SendMessageW(c,TBM_GETPOS,0,0));g.st.values[i]=k.slider.has_off&&p<k.slider.min?0:std::clamp(k.slider.min+((p-k.slider.min+k.slider.step/2)/k.slider.step)*k.slider.step,k.slider.min,k.slider.max);footer();if(LOWORD(w)==TB_ENDTRACK){int previous=g.scroll;rebuild();scroll_to(previous);SetFocus(control(g.body,KNOB+i));}}return 0;}
    case WM_VSCROLL:{SCROLLINFO si{sizeof si,SIF_ALL};GetScrollInfo(h,SB_VERT,&si);int n=g.scroll;switch(LOWORD(w)){case SB_LINEUP:n-=S(40);break;case SB_LINEDOWN:n+=S(40);break;case SB_PAGEUP:n-=si.nPage;break;case SB_PAGEDOWN:n+=si.nPage;break;case SB_THUMBTRACK:n=si.nTrackPos;break;}scroll_to(n);return 0;}
    case WM_MOUSEWHEEL:scroll_to(g.scroll-GET_WHEEL_DELTA_WPARAM(w)*S(96)/WHEEL_DELTA);return 0;
    case WM_CTLCOLORBTN:if(GetDlgCtrlID((HWND)l)!=CLEAN)break;[[fallthrough]];
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:SetTextColor((HDC)w,foreground());SetBkColor((HDC)w,background());return LRESULT(g.bg);
  }return DefWindowProcW(h,m,w,l);
}
static LRESULT CALLBACK emblemproc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR) {
  if((m==WM_PAINT||m==WM_PRINTCLIENT)&&g.high_contrast){
    PAINTSTRUCT ps{};HDC dc=m==WM_PAINT?BeginPaint(h,&ps):(HDC)w;
    RECT r{};GetClientRect(h,&r);FillRect(dc,&r,g.bg);
    HBRUSH brush=CreateSolidBrush(foreground());auto old=SelectObject(dc,brush);auto pen=SelectObject(dc,GetStockObject(NULL_PEN));
    // assets/branding/bg3rtx-small.svg; solid B1 geometry only.
    const std::vector<std::vector<POINT>> paths={{{7,1},{3,1},{1,3},{1,11},{4,14},{6,14},{6,12},{5,12},{3,10},{3,4},{4,3},{7,3}},{{10,2},{12,2},{15,5},{15,13},{13,15},{9,15},{9,13},{12,13},{13,12},{13,6},{11,4},{10,4}},{{7,6},{9,6},{9,8},{7,8}},{{7,9},{9,9},{10,12},{6,12}}};
    for(auto points:paths){for(auto& p:points){p.x=p.x*r.right/16;p.y=p.y*r.bottom/16;}Polygon(dc,points.data(),int(points.size()));}
    SelectObject(dc,pen);SelectObject(dc,old);DeleteObject(brush);if(m==WM_PAINT)EndPaint(h,&ps);return 0;
  }
  return DefSubclassProc(h,m,w,l);
}
static LRESULT CALLBACK wndproc(HWND h,UINT m,WPARAM w,LPARAM l) {
  switch(m){
    case WM_COMMAND:command(LOWORD(w),HIWORD(w),(HWND)l);return 0;
    case WM_NOTIFY:if(((NMHDR*)l)->idFrom==TABS&&((NMHDR*)l)->code==TCN_SELCHANGE){g.tab=g_tab_ids[TabCtrl_GetCurSel(control(h,TABS))];g.scroll=0;rebuild();}return 0;
    case WM_SIZE:layout();return 0;
    case WM_GETMINMAXINFO:((MINMAXINFO*)l)->ptMinTrackSize={S(736),S(620)};return 0;
    case WM_MOUSEWHEEL:return bodyproc(g.body,m,w,l);
    case WM_TIMER:{
      auto session=game_session();
      if(session!=g.session){g.session=session;g.baseline=saved_settings();g.pending=false;std::wstring err;if(!persist(&err))g.message=L"Restart tracking could not be stored. "+err;}
      footer();return 0;}
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:SetTextColor((HDC)w,foreground());SetBkColor((HDC)w,background());return LRESULT(g.bg);
    case WM_CLOSE:
      if(dirty_count()){int r=MessageBoxW(h,L"Save your changes before closing?\nYes: Save   No: Discard   Cancel: Keep editing",L"Unsaved changes",MB_YESNOCANCEL|MB_ICONQUESTION);if(r==IDCANCEL||(r==IDYES&&!save(false)))return 0;}
      DestroyWindow(h);return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
  }return DefWindowProcW(h,m,w,l);
}
static void screenshot(const std::wstring& path) {
  RECT r{};GetWindowRect(g.wnd,&r);int w=r.right-r.left,h=r.bottom-r.top;
  HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);BITMAPINFO bi{};bi.bmiHeader={sizeof(BITMAPINFOHEADER),w,-h,1,32,BI_RGB};void* bits=nullptr;
  HBITMAP bmp=CreateDIBSection(screen,&bi,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(dc,bmp);
  SendMessageW(g.wnd,WM_PRINT,WPARAM(dc),PRF_CLIENT|PRF_NONCLIENT|PRF_CHILDREN|PRF_ERASEBKGND);GdiFlush();BITMAPFILEHEADER fh{0x4D42,DWORD(sizeof fh+sizeof bi.bmiHeader+size_t(w)*h*4),0,0,DWORD(sizeof fh+sizeof bi.bmiHeader)};
  std::string data((char*)&fh,sizeof fh);data.append((char*)&bi.bmiHeader,sizeof bi.bmiHeader);data.append((char*)bits,size_t(w)*h*4);write_file_atomic(path,data,nullptr);
  SelectObject(dc,old);DeleteObject(bmp);DeleteDC(dc);ReleaseDC(nullptr,screen);
}
static int run_window(HINSTANCE inst,const std::wstring& game_arg) {
  CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);INITCOMMONCONTROLSEX icc{sizeof icc,ICC_STANDARD_CLASSES|ICC_BAR_CLASSES|ICC_TAB_CLASSES};InitCommonControlsEx(&icc);
  g.scale=g_screenshot_scale>0?g_screenshot_scale:float(GetDpiForSystem())/96;
  HIGHCONTRASTW hc{sizeof hc};SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof hc,&hc,0);g.high_contrast=(hc.dwFlags&HCF_HIGHCONTRASTON)!=0||g_review==L"contrast";
  std::wstring font_dir=exe_dir()+L"\\ui\\fonts";
  if(!file_exists(font_dir+L"\\LatoWeb-Regular.ttf"))font_dir=root_dir()+L"\\assets\\fonts";
  std::wstring font_path=font_dir+L"\\LatoWeb-Regular.ttf";bool have_font=AddFontResourceExW(font_path.c_str(),FR_PRIVATE,nullptr)>0;
  std::wstring semibold_path=font_dir+L"\\LatoWeb-Semibold.ttf";AddFontResourceExW(semibold_path.c_str(),FR_PRIVATE,nullptr);
  g.font=CreateFontW(-S(15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,g.high_contrast||!have_font?L"Segoe UI":L"Lato");
  g.bold=CreateFontW(-S(21),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,g.high_contrast||!have_font?L"Segoe UI":L"Lato");g.bg=CreateSolidBrush(background());
  load_state(g.st);g.st.caps=0;detect_gpu(g.st,&g.gpu_err);g.dev=st::dev_mode(st::lookup(saved_settings(),"dev"));
  g_inspection=inspect_graphics();g_adapter=g_inspection.adapters.size()==1?0:-1;g.st.caps=assess_compatibility(g_inspection,g_adapter).caps;
  g.st.values=st::presentation_values(saved_settings(),g.dev);g.saved=g.st.values;g.saved_preset=g.st.preset;
  g.dir=game_arg.empty()?saved_game_dir():game_arg;if(!valid_game_dir(g.dir)&&g_screenshot.empty())g.dir=detect_game_dir();g.saved_dir=g.dir;
  std::string state;read_file(ui_path(),state);auto config=st::parse_ini(state);g.stage=std::clamp(std::atoi(st::lookup(config,"onboarding").c_str()),0,4);
  if(g.stage<4&&game_arg.empty()&&!st::lookup(config,"onboarding_game").empty())g.dir=widen(st::lookup(config,"onboarding_game"));
  g.session=game_session();g.baseline=saved_settings();if(launcher::ui_state::same_session(st::lookup(config,"session"),g.session)){
    g.pending=st::lookup(config,"pending_restart")=="1";
    for(auto& kv:config)if(kv.first.rfind("baseline_",0)==0)g.baseline[kv.first.substr(9)]=kv.second;
  }
  if(!g_review.empty()){
    g.stage=g_review==L"first-run"?0:4;g.dev=false;g.st.values=st::presentation_values(saved_settings(),false);g.saved=g.st.values;
  }
  if(g_review==L"restart"){g.tab=1;g.baseline["rt_alpha"]=st::lookup(saved_settings(),"rt_alpha")=="0"?"1":"0";g.pending=true;}
  if(g_review==L"graphics")g.tab=1;
  if(g_review==L"advanced"){g.dev=true;g.tab=3;}
  if(g_review==L"support")g.tab=4;
  if(g_review==L"experimental"){g.dev=true;g.tab=3;g.experimental=true;}
  if(g_review==L"missing")g.dir=L"";
  if(g_review==L"probe"){g.st.have_gpu=false;g.st.caps=0;g.gpu_err=L"The graphics probe returned no adapter. Recheck or report a problem";}
  if(g_review==L"error")g.message=L"Settings saved; game could not start. The selected folder is unavailable or access was denied. Choose a folder in Overview, retry, or use Support > Report a problem. Windows error: access denied (5). Your saved settings are kept.";
  WNDCLASSEXW wc{sizeof wc};wc.lpfnWndProc=wndproc;wc.hInstance=inst;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=g.bg;wc.lpszClassName=L"BG3RTXLauncher";
  wc.hIcon=(HICON)LoadImageW(inst,MAKEINTRESOURCEW(1),IMAGE_ICON,S(32),S(32),0);wc.hIconSm=wc.hIcon;RegisterClassExW(&wc);
  wc.lpfnWndProc=bodyproc;wc.lpszClassName=L"BG3RTXBody";RegisterClassExW(&wc);
  g.wnd=CreateWindowExW(WS_EX_CONTROLPARENT,L"BG3RTXLauncher",L"BG3RTX settings",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,S(g_review_width?g_review_width+16:960),S(900),nullptr,nullptr,inst,nullptr);
  auto emblem=add(g.wnd,L"STATIC",L"",SS_ICON,0);SendMessageW(emblem,STM_SETICON,WPARAM(wc.hIcon),0);SetWindowSubclass(emblem,emblemproc,1,0);place(emblem,16,8,32,32);
  label(g.wnd,L"BG3RTX " + widen(BG3RTX_PRODUCT_VERSION) + L" | Independent fan mod",64,8,660,32,true);label(g.wnd,L"Preset",16,60,72,24);
  for(int p=0;p<st::kPresetCount;++p)add(g.wnd,L"BUTTON",widen(st::kPresetLabel[p]),WS_TABSTOP|BS_AUTORADIOBUTTON|BS_PUSHLIKE|(p==0?WS_GROUP:0),PRESET_LOW+p);
  add(g.wnd,L"BUTTON",L"Use recommended",WS_TABSTOP,SUGGEST);add(g.wnd,L"STATIC",L"Suitability: estimate\nCoverage: RTX 5090 reference",SS_LEFT,NOTE);
  auto tabs=add(g.wnd,WC_TABCONTROLW,L"",WS_TABSTOP,TABS);
  for(auto entry:{std::pair<int,const wchar_t*>{0,L"Overview"},{1,L"Lighting"},{6,L"Surfaces"},{2,L"Performance"},{3,L"Advanced"},{4,L"Help & support"},{5,L"Developer"}}){
    if((entry.first==3||entry.first==5)&&!g.dev)continue;
    TCITEMW ti{};ti.mask=TCIF_TEXT;ti.pszText=const_cast<LPWSTR>(entry.second);int row=TabCtrl_GetItemCount(tabs);TabCtrl_InsertItem(tabs,row,&ti);g_tab_ids.push_back(entry.first);if(entry.first==g.tab)TabCtrl_SetCurSel(tabs,row);
  }
  g.body=CreateWindowExW(WS_EX_CONTROLPARENT,L"BG3RTXBody",L"Settings section",WS_CHILD|WS_VISIBLE|WS_VSCROLL,0,0,0,0,g.wnd,nullptr,inst,nullptr);
  add(g.wnd,L"STATIC",L"",SS_LEFT,STATUS);add(g.wnd,L"BUTTON",L"Details / report",WS_TABSTOP,ERROR_DETAILS);for(auto p:{std::pair<int,const wchar_t*>{RESET,L"Reset entire preset"},{DISCARD,L"Discard"},{SAVE,L"Save"},{PLAY,L"Play"},{CLOSE,L"Close"}})add(g.wnd,L"BUTTON",p.second,WS_TABSTOP,p.first);
  EnableWindow(control(g.wnd,SUGGEST),g.st.have_gpu);
  if(!g.st.have_gpu)SetWindowTextW(control(g.wnd,NOTE),L"Compatibility: Unknown\nRecheck in Overview");
  layout();
  if(g_screenshot.empty()){std::wstring err;if(!persist(&err))status(L"Setup state could not be stored. "+err);}
  if(!g_screenshot.empty()){
    SetWindowPos(g.wnd,nullptr,-20000,-20000,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);ShowWindow(g.wnd,SW_SHOWNOACTIVATE);if(g_screenshot_bottom)scroll_to(g.height);
    RedrawWindow(g.wnd,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);screenshot(g_screenshot);DestroyWindow(g.wnd);
  }else{SetTimer(g.wnd,1,2000,nullptr);ShowWindow(g.wnd,SW_SHOW);MSG m;while(GetMessageW(&m,nullptr,0,0)>0){if(IsDialogMessageW(g.wnd,&m))continue;TranslateMessage(&m);DispatchMessageW(&m);}}
  DeleteObject(g.font);DeleteObject(g.bold);DeleteObject(g.bg);RemoveFontResourceExW(font_path.c_str(),FR_PRIVATE,nullptr);RemoveFontResourceExW(semibold_path.c_str(),FR_PRIVATE,nullptr);CoUninitialize();return 0;
}
