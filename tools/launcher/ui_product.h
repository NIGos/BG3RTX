// Native launcher presentation. Included by main.cpp; no extra build registration.
#include <tlhelp32.h>
#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")
#include "ui_state.h"
#include "version.h"
#include "compatibility.h"
#include "../../common/experimental_ab.h"
#include "../../common/hotkeys.h"

static std::wstring g_screenshot, g_review;
static std::vector<int> g_tab_ids;
static GraphicsInspection g_inspection;
static int g_adapter=-1;
static bool g_screenshot_bottom=false, g_no_game=false;
static float g_screenshot_scale=0;
static int g_review_width=0;
enum { PRESET=20, SUGGEST, TABS, EXPAND, SAVE, PLAY, DISCARD, CLOSE, REPORT, BROWSE,
       GAME, STATUS, NOTE, NEXT, RETRY, LOG, FOLDER, RESET, ERROR_DETAILS, ADAPTER, NOTICES, HEADER, NR_BROWSE, NR_CLEAR, HK_RT=50, HK_NR, HK_RT_RESET, HK_NR_RESET, PRESET_LOW=60, KNOB=1000, DETAILS=2000 };
struct Ui {
  HWND wnd{}, body{}; HFONT font{}, bold{}; HBRUSH bg{};
  bg3::experimental_ab::Selection ab_selection;
  State st; st::Map baseline; std::vector<int> saved;
  int saved_preset{}, tab=0, scroll=0, height=0, stage=0;
  float scale=1; bool dev=false, experimental=false, saved_experimental=false, updating=false, pending=false, high_contrast=false;
  std::wstring dir, saved_dir, gpu_err, message; std::string session;
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
static void label(HWND p,const std::wstring& text,int x,int y,int w,int h,bool bold=false) {place(add(p,L"STATIC",text,SS_LEFT|SS_NOPREFIX,0,bold),x,y,w,h);}
static void button(HWND p,int id,const std::wstring& text,int x,int y,int w) {place(add(p,L"BUTTON",text,WS_TABSTOP|BS_PUSHBUTTON,id),x,y,w,32);}
static st::Map saved_settings() { st::Map m; st::apply_layer(m,g.st.base);st::apply_layer(m,g.st.user);return m; }
static st::Map effective_settings() {
  auto m=saved_settings();m["dlss5_nr_runtime"]=g.st.dlss5_runtime_path;for(int i=0;i<st::kKnobCount;++i){st::KVList v;st::knob_kvs(st::kKnobs[i],g.st.values[i],v);for(auto& kv:v)m[kv.first]=kv.second;}return m;
}
static bool opt_in() { return !g.dev && g.experimental; }
static int changed_shortcuts() {int n=0;for(size_t i=0;i<g.st.shortcuts.size();++i)n+=int(g.st.shortcuts[i]!=g.st.loaded_shortcuts[i]);return n;}
static int dirty_count() {return std::max(int(g.ab_selection.pending || g.experimental!=g.saved_experimental),launcher::ui_state::dirty(g.st.values,g.saved,g.st.preset,g.saved_preset,g.dir,g.saved_dir)+int(g.st.dlss5_runtime_path!=g.st.loaded_dlss5_runtime_path)+changed_shortcuts());}
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
  int count=dirty_count();
  std::wstring t=count?std::to_wstring(count)+(count==1?L" unsaved change. ":L" unsaved changes. "):L"";
  bool running=game_running();
  int pending=(g.pending&&(running||!g_screenshot.empty()))?launcher::ui_state::restart_count(g.baseline,saved_settings()):0;
  t+=pending?widen(launcher::ui_state::restart_banner(pending))+L" ":count?L"":L"Saved. ";
  if(running)t+=L"Game is running. ";
  if(!g.message.empty()&&g.message!=L"Saved.")t+=L"\r\n"+(g.message.size()>220?g.message.substr(0,220)+L"... (View error details)":g.message);
  ShowWindow(control(g.wnd,ERROR_DETAILS),g.message.empty()?SW_HIDE:SW_SHOW);
  SetWindowTextW(control(g.wnd,STATUS),t.c_str());
  auto preset=st::preset_values_for(g.st.preset,g.st.caps);int custom=0;
  for(int i=0;i<st::kKnobCount;++i)if(st::knob_visible(st::kKnobs[i],g.dev,opt_in())&&std::string(st::kKnobs[i].category)!="Support")custom+=g.st.values[i]!=preset[i];
  std::wstring note=!g.st.have_gpu?L"Compatibility not checked\nRecheck graphics in Help & support":custom?L"Custom: "+std::to_wstring(custom)+(custom==1?L" setting":L" settings"):L"Matches preset";
  std::wstring header=L"BG3RTX "+widen(launcher::product_version())+L" | "+(custom?L"Custom":widen(st::kPresetLabel[g.st.preset]))+L" | "+(g.st.have_gpu?widen(g.st.gpu.name):L"GPU not detected")+L" | "+(file_exists(exe_dir()+L"\\VkLayer_bg3rtx.dll")&&file_exists(exe_dir()+L"\\bg3rtx_core.dll")?L"Mod files present":L"Mod files missing");
  SetWindowTextW(control(g.wnd,HEADER),header.c_str());
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
  const int id=GetDlgCtrlID(h);
  if((id==HK_RT||id==HK_NR)&&(m==WM_KEYDOWN||m==WM_SYSKEYDOWN) && !(l&(1ll<<30))) {
    if(w==VK_TAB)return DefSubclassProc(h,m,w,l);
    int i=id==HK_RT?0:1;
    bg3::hotkeys::Key k{};
    if(w==VK_DELETE||w==VK_BACK)k={0,0,true};
    else if((w>=VK_F1&&w<=VK_F12)||(w>='A'&&w<='Z')||(w>='0'&&w<='9'))
      k={unsigned(w),unsigned(((GetKeyState(VK_CONTROL)&0x8000)?1:0)|((GetKeyState(VK_SHIFT)&0x8000)?2:0)|((GetKeyState(VK_MENU)&0x8000)?4:0)),true};
    if(k.valid){auto value=bg3::hotkeys::format(k);
      auto error=bg3::hotkeys::validate_change_at(g.st.shortcuts.data(),i,value);
      if(error.empty()){g.st.shortcuts[i]=value;SetWindowTextW(h,widen(value).c_str());status(L"Keyboard shortcut changed. Save to apply.");}
      else status(widen(error));
    }
    return 0;
  }
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
  place(control(g.wnd,HEADER),64,8,w-80,40);place(control(g.wnd,NOTE),584,56,w-600,40);place(control(g.wnd,TABS),16,104,w-32,32);
  place(g.body,0,144,w,h-296);place(control(g.wnd,STATUS),16,h-144,w-32,80);place(control(g.wnd,ERROR_DETAILS),16,h-48,160,32);
  int x=w-16;for(auto pair:{std::pair<int,int>{CLOSE,80},{PLAY,96},{SAVE,80},{DISCARD,136},{RESET,176}}){x-=pair.second;place(control(g.wnd,pair.first),x,h-48,pair.second,32);x-=8;}
  rebuild();
}
static bool matches(const st::Knob& k) {
  if(!st::knob_visible(k,g.dev,opt_in()))return false;
  if(k.dev_only && !(opt_in() && st::opt_in_knob(k)))return g.tab==5;
  if(k.experimental&&std::string(k.id)!="dlss5_nr")return g.tab==3&&g.experimental;
  std::string c=k.category;
  return (g.tab==1&&c=="Lighting")||(g.tab==6&&c=="Surfaces")||(g.tab==2&&c=="Performance")||(g.tab==3&&c=="Advanced")||(g.tab==7&&c=="DLSS 5");
}
static std::wstring choice_label(const st::Knob& k,int c) {
  if(c<0)return L"Custom value preserved";
  std::wstring s=widen(k.choices[c].label);
  std::string maturity=st::choice_maturity(k,c);
  if(maturity=="awaiting_qualification"||maturity=="experimental")s+=L" (Experimental)";
  if(!st::choice_supported(k,c,g.st.caps))s+=L" (unavailable)";
  return s;
}
static void rebuild() {
  if(!g.body)return;HWND focused=GetFocus();int focus_id=GetParent(focused)==g.body?GetDlgCtrlID(focused):0;int previous_scroll=g.scroll;g.updating=true;g.scroll=0;
  while(HWND h=GetWindow(g.body,GW_CHILD))DestroyWindow(h);
  int w=body_width(), y=16;
  if(g.tab==0) {
    static const wchar_t* steps[]={L"BG3RTX setup",L"Choose game folder",L"Check graphics",L"Ready to play"};
    label(g.body,g.stage<4?steps[g.stage]:L"Overview",16,y,w,32,true);y+=40;
    std::wstring intro=g.stage==0?L"Review your settings, then choose Play to start the game.":
      g.stage==1?L"Choose the game folder containing bin\\bg3.exe. You can choose the game folder later. Launches the Vulkan version.":
      g.stage==2?L"Review the suggested preset; performance varies by scene.":
      L"Enable DLSS in the game's Graphics options to use DLSS Ray Reconstruction; Quality is suggested.";
    label(g.body,intro,16,y,w,64);y+=72;

    button(g.body,RETRY,L"Recheck graphics",16,y,176);y+=48;
    label(g.body,L"Game folder",16,y,w,24,true);y+=32;
    place(add(g.body,L"EDIT",g.dir,WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,GAME),16,y,w-136,32);
    button(g.body,BROWSE,L"Choose folder…",w-104,y,120);y+=48;
    label(g.body,valid_game_dir(g.dir)?L"Game found.":L"Game not found. Choose its folder to enable Play.",16,y,w,56);y+=64;
    label(g.body,widen(st::kPresetDesc[g.st.preset]),16,y,w,96);y+=104;
    if(g.stage<4){button(g.body,NEXT,g.stage==3?L"Finish setup":L"Continue",16,y,176);y+=48;}
    label(g.body,L"This is the basic window. RTX Hair preparation and its keyboard shortcut are only in the full launcher window.",16,y,w,48);y+=56;
    // Same report flow as Help & support, linked from the first page.
    button(g.body,REPORT,L"Problem? Create a diagnostic report…",16,y,300);y+=48;
  } else if(g.tab==4) {
    label(g.body,L"Help & support",16,y,w,32,true);y+=40;
    label(g.body,L"Local report",16,y,w,28,true);y+=36;
    label(g.body,L"Preview logs, settings and graphics details before saving a report on your PC. Nothing uploads automatically.",16,y,w,72);y+=80;
    button(g.body,REPORT,L"Report a problem",16,y,200);y+=48;
    {const int i=st::knob_index("detailed_logging");const auto& k=st::kKnobs[i];
      label(g.body,widen(k.label),16,y,w,28,true);y+=36;
      label(g.body,widen(k.desc),16,y,w,64);y+=72;
      HWND c=add(g.body,WC_COMBOBOXW,L"",WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST,KNOB+i);place(c,16,y,200,240);
      for(int ch=0;ch<st::choice_count(k);++ch){int row=int(SendMessageW(c,CB_ADDSTRING,0,LPARAM(widen(k.choices[ch].label).c_str())));SendMessageW(c,CB_SETITEMDATA,row,ch);if(g.st.values[i]==ch)SendMessageW(c,CB_SETCURSEL,row,0);}
      y+=40;label(g.body,L"Restart the game to apply",16,y,w,28);y+=40;
    }

    label(g.body,L"Game and graphics",16,y,w,28,true);y+=36;
    label(g.body,L"Choose the game folder containing bin\\bg3.exe. Launches the Vulkan version.",16,y,w,48);y+=56;
    button(g.body,BROWSE,L"Choose game folder…",16,y,224);y+=48;
    button(g.body,RETRY,L"Recheck graphics",16,y,224);y+=48;
    label(g.body,L"If the game does not start or Frame Generation does nothing, turn off other overlays (Steam, Discord, GOG Galaxy) for one launch.",16,y,w,96);y+=104;
    const auto compatibility=assess_compatibility(g_inspection,g_adapter);
    label(g.body,L"Compatibility",16,y,w,28,true);y+=36;
    label(g.body,widen(compatibility.hardware_text+"\n"+compatibility.driver_text+"\n"+compatibility.adapter_text),16,y,w,120);y+=128;
    label(g.body,L"Video memory",16,y,w,28,true);y+=36;
    label(g.body,widen(compatibility.memory_text),16,y,w,144);y+=152;
    if(g_inspection.adapters.size()>1){
      label(g.body,L"GPU",16,y,w,28,true);y+=36;
      HWND adapter=add(g.body,WC_COMBOBOXW,L"",WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST,ADAPTER);place(adapter,16,y,w,240);
      SendMessageW(adapter,CB_ADDSTRING,0,LPARAM(L"Choose GPU"));
      for(const auto& a:g_inspection.adapters)SendMessageW(adapter,CB_ADDSTRING,0,LPARAM(widen(a.gpu.name).c_str()));
      SendMessageW(adapter,CB_SETCURSEL,g_adapter+1,0);y+=48;
    }
    label(g.body,L"Comparison and overlays",16,y,w,28,true);y+=36;
    label(g.body,L"By default, Ctrl+Shift+F9 in game switches all ray-traced lighting off and on. Change this shortcut on Advanced.",16,y,w,48);y+=56;
    place(add(g.body,WC_LINK,L"<a>Fonts and third-party notices</a>",WS_TABSTOP,NOTICES),16,y,w,32);y+=48;
    label(g.body,L"Independent fan mod; not affiliated with the game's developers.",16,y,w,48);y+=56;
  } else {
    if(g.tab==3) {
      int enabled=0;for(int i=0;i<st::kKnobCount;++i)if(g.st.values[i]>0&&std::string(st::choice_maturity(st::kKnobs[i],g.st.values[i]))=="experimental")++enabled;
      button(g.body,EXPAND,(g.experimental?L"Hide Experimental":L"Show Experimental")+std::wstring(L" (")+std::to_wstring(enabled)+L" enabled)",16,y,w);y+=48;
    }
    std::vector<int> rows;
    for(const char* id:{"rt_geometry","rr","sun_shadows","gi","exp_hidden_surface","local_shadows","box_lights","exp_native_coverage","rt_intensity"})rows.push_back(st::knob_index(id));
    for(int i=0;i<st::kKnobCount;++i)if(std::find(rows.begin(),rows.end(),i)==rows.end())rows.push_back(i);
    std::string category;
    for(int i:rows) {
      const auto& k=st::kKnobs[i];if(!matches(k))continue;
      if(category!=k.category){category=k.category;label(g.body,widen(category),16,y,w,28,true);y+=40;}
      label(g.body,widen(k.label),32,y,w-32,28,true);y+=36;
      HWND c;
      if(k.kind==st::Kind::Choice){
        c=add(g.body,WC_COMBOBOXW,L"",WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST,KNOB+i);place(c,32,y,w-32,240);
        auto append=[&](int v){int row=int(SendMessageW(c,CB_ADDSTRING,0,LPARAM(choice_label(k,v).c_str())));SendMessageW(c,CB_SETITEMDATA,row,v);if(v==g.st.values[i])SendMessageW(c,CB_SETCURSEL,row,0);};
        if(g.st.values[i]<0)append(-1);
        for(int ch=0;ch<st::choice_count(k);++ch)if(st::choice_visible(k,ch,g.dev,opt_in())&&(g.experimental||!st::choice_requires_experimental(k,ch)))append(ch);
        if(g.st.values[i]>=0&&SendMessageW(c,CB_GETCURSEL,0,0)==CB_ERR){int row=int(SendMessageW(c,CB_ADDSTRING,0,LPARAM(L"Custom value preserved")));SendMessageW(c,CB_SETITEMDATA,row,-1);SendMessageW(c,CB_SETCURSEL,row,0);}
      } else {
        c=add(g.body,TRACKBAR_CLASSW,L"",WS_TABSTOP|TBS_HORZ|TBS_NOTICKS,KNOB+i);place(c,32,y,w-160,32);
        SendMessageW(c,TBM_SETRANGE,TRUE,MAKELPARAM(k.slider.has_off?k.slider.min-1:k.slider.min,k.slider.max));SendMessageW(c,TBM_SETPOS,TRUE,g.st.values[i]<0?k.slider.max:(k.slider.has_off&&g.st.values[i]==0?k.slider.min-1:g.st.values[i]));
        SetWindowTextW(c,widen(k.label).c_str());label(g.body,g.st.values[i]<0?L"Custom":g.st.values[i]==0?L"Off":std::to_wstring(g.st.values[i])+(std::string(k.slider.unit)=="%"?L"":L" ")+widen(k.slider.unit),w-112,y,112,32);
      }
      y+=48;label(g.body,widen(k.desc),32,y,w-32,72);y+=80;
      if(k.dev_only){
        std::wstring badge=L"GPU cost: "+(std::string(k.cost.gpu)=="Unknown"||std::string(k.cost.gpu)=="Not measured"?std::wstring(L"Not measured"):widen(k.cost.gpu))+L" | VRAM: "+(std::string(k.cost.vram)=="Unknown"?std::wstring(L"Unknown"):widen(k.cost.status)+L" "+widen(k.cost.vram));
        label(g.body,badge,32,y,w-32,48);y+=56;
      }
      auto need=st::unmet_need(k,g.st.values[i],g.st.values);if(need.empty())need=st::unmet_key_need(k,g.st.values[i],effective_settings());
      label(g.body,widen(st::apply_label(st::transition_apply(k,i<int(g.saved.size())?g.saved[i]:-1,g.st.values[i]),!k.dev_only)),32,y,w-32,28,true);y+=36;
      for(int ch=0;ch<st::choice_count(k);++ch)if(st::choice_visible(k,ch,g.dev,opt_in())){
        for(const auto& n:k.choices[ch].need)if(n.knob){auto j=st::knob_index(n.knob);if(j>=0){if(!need.empty())need+="\n";need+=std::string(k.choices[ch].label)+(k.dev_only?": requires ":": Requires ")+st::kKnobs[j].label+(k.dev_only?" ":": ")+st::kKnobs[j].choices[n.min_choice].label;}}
      }
      for(const auto& n:k.required_on)if(n.key){if(!need.empty())need+="\n";need+="Requires "+std::string(n.label);}
      if(!need.empty()){int lines=1+int(std::count(need.begin(),need.end(),'\n'));int height=24*lines+24*int(need.size()/std::max(30,w/9));label(g.body,widen(need),32,y,w-32,height);y+=height+8;}
      if(std::string(k.id)=="dlss5_nr"){
        label(g.body,L"DLSS 5 runtime file",32,y,w-32,24,true);y+=28;
        place(add(g.body,L"EDIT",g.st.dlss5_runtime_path.empty()?L"Automatic: game folder":widen(g.st.dlss5_runtime_path),WS_BORDER|ES_READONLY|ES_AUTOHSCROLL,0),32,y,w-32,30);y+=36;
        button(g.body,NR_BROWSE,L"Browse...",32,y,116);button(g.body,NR_CLEAR,L"Automatic discovery",160,y,200);y+=40;
        label(g.body,widen(g.st.dlss5_runtime_status.empty()?"Runtime status is checked after choosing a file. NVIDIA-signed candidates are preferred.":g.st.dlss5_runtime_status),32,y,w-32,56);y+=64;
      }
      button(g.body,DETAILS+i,L"Details and requirements",32,y,256);y+=48;place(add(g.body,L"STATIC",L"",SS_ETCHEDHORZ,0),16,y,w,2);y+=24;
    }
  }
  if(g.tab==3||g.tab==7){int i=g.tab==3?0:1;
    label(g.body,L"Keyboard shortcuts",32,y,w-32,28,true);y+=36;
    label(g.body,i==0?L"All ray tracing off/on":L"DLSS 5 off/on",32,y,w-32,24);y+=28;
    place(add(g.body,L"EDIT",widen(g.st.shortcuts[i]),WS_TABSTOP|WS_BORDER|ES_READONLY,i==0?HK_RT:HK_NR),32,y,w-232,30);
    button(g.body,i==0?HK_RT_RESET:HK_NR_RESET,L"Reset to default",w-192,y,192);y+=40;
    label(g.body,L"Press a key combination in the field. Delete or Backspace disables it.",32,y,w-32,48);y+=56;
  }
  g.height=S(y+16);scroll_to(previous_scroll);g.updating=false;footer();if(focus_id)if(HWND target=control(g.body,focus_id))SetFocus(target);
}
static void browse() {
  IFileOpenDialog* d=nullptr;if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&d))))return;
  DWORD opts=0;d->GetOptions(&opts);d->SetOptions(opts|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);d->SetTitle(L"Choose game folder containing bin\\bg3.exe");
  if(SUCCEEDED(d->Show(g.wnd))){IShellItem* it=nullptr;if(SUCCEEDED(d->GetResult(&it))){PWSTR p=nullptr;if(SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH,&p))){auto dir=game_dir_from_path(p);g.dir=dir.empty()?p:dir;g.st.dlss5_runtime_status=launcher::dlss5::discovery_status(g.st,g.dir);CoTaskMemFree(p);rebuild();}it->Release();}}d->Release();
}
static void report_problem() { launcher::web::native_report(g.wnd); }
static bool save(bool) {
  auto shortcut_error=bg3::hotkeys::validate_all(g.st.shortcuts.data());
  if(!shortcut_error.empty()){status(widen(shortcut_error));return false;}
  if(!g.st.settings_readable){status(L"Settings could not be read. Close and retry before saving.");return false;}
  auto preview=st::preview_dependencies(g.st.values,effective_settings(),g.st.caps,false,g.dev,g.experimental,opt_in());
  if(!preview.changed.empty()){
    int policy=launcher::web::action_dialog(g.wnd,L"Review required settings",L"These options need other settings.",{{IDYES,L"Enable requirements"},{IDNO,L"Adjust options"},{IDCANCEL,L"Keep editing"}});
    if(policy==IDCANCEL)return false;
    if(policy==IDYES)preview=st::preview_dependencies(g.st.values,effective_settings(),g.st.caps,true,g.dev,g.experimental,opt_in());
  }
  bool adjustments=!preview.changed.empty()||!preview.required_keys.empty();
  if(adjustments){
    std::wstring text=L"Review required settings:\n";
    for(int i:preview.changed)text+=L"\n"+widen(st::kKnobs[i].label)+L": "+choice_label(st::kKnobs[i],g.st.values[i])+L" -> "+choice_label(st::kKnobs[i],preview.values[i]);
    for(auto& kv:preview.required_keys){
      std::wstring label=L"Required startup setting";
      for(int i=0;i<st::kKnobCount;++i){const auto& k=st::kKnobs[i];if(!st::knob_visible(k,g.dev,opt_in()))continue;
        for(const auto& n:k.required_on)if(n.key&&kv.first==n.key&&preview.values[i]>=n.min_choice){label=widen(std::string(k.label)+": "+n.label);break;}
        if(label!=L"Required startup setting")break;
      }
      text+=L"\n"+label+L": required setup applies after restarting the game";
    }
    text+=L"\n\nCancel keeps your edits.";
    if(launcher::web::action_dialog(g.wnd,L"Review required settings",text,{{IDOK,L"Apply changes"},{IDCANCEL,L"Cancel"}})!=IDOK)return false;
  }
  if(!preview.blocked.empty()){std::wstring t=L"Settings not saved. Check these requirements:\n";for(auto& b:preview.blocked)t+=L"\n"+widen(b);MessageBoxW(g.wnd,t.c_str(),L"Review requirements",MB_OK|MB_ICONWARNING);return false;}
  State candidate=g.st;candidate.values=preview.values;
  auto forced=forced_keys(candidate);forced.insert(forced.end(),preview.required_keys.begin(),preview.required_keys.end());
  g.ab_selection.append(forced);
  if(!g.dev)forced.emplace_back("experimental_options",g.experimental?"1":"0");
  auto text=st::render_user_ini(candidate.user_text,candidate.preset,candidate.values,forced,st::layers_dev(candidate.base,st::parse_ini(candidate.user_text)),&candidate.loaded);
  auto after=st::parse_ini(text);st::Map effective;st::apply_layer(effective,candidate.base);st::apply_layer(effective,after);
  bool old_pending=g.pending;g.pending=game_running()&&needs_restart(g.baseline,effective);
  std::wstring err;
  // Persist the warning before the settings transaction; a failed settings write may leave a conservative warning.
  if(!persist(&err)){g.pending=old_pending;status(L"Settings not saved. Restart information could not be saved. "+err);return false;}
  if(!write_file_atomic(user_ini_path(),text,&err)){status(L"Settings not saved. Your edits are kept. "+err);return false;}
  g.ab_selection.pending=false;
  g.st.user_text=text;g.st.user=after;g.st.values=preview.values;g.st.loaded=preview.values;g.st.loaded_dlss5_runtime_path=g.st.dlss5_runtime_path;for(int i=0;i<bg3::hotkeys::kCount;++i)g.st.loaded_shortcuts[i]=g.st.shortcuts[i];
  if(!save_game_dir(g.dir,&err)){status(L"Game folder not saved. "+err);return false;}
  g.saved_experimental=g.experimental;g.saved=g.st.values;g.saved_preset=g.st.preset;g.saved_dir=g.dir;
  status(L"Saved.");rebuild();return true;
}
static void select_preset(int p) {
  auto v=st::preset_values_for(p,g.st.caps);
  // Preset review replaces visible custom selections; hidden controls stay intact.
  for(int i=0;i<st::kKnobCount;++i)if(!st::knob_visible(st::kKnobs[i],g.dev,opt_in())||std::string(st::kKnobs[i].category)=="Support")v[i]=g.st.values[i];
  std::wstring t=L"Review preset: "+widen(st::kPresetLabel[p])+L"\n";
  for(int i=0;i<st::kKnobCount;++i)if(v[i]!=g.st.values[i])t+=L"\n"+widen(st::kKnobs[i].label)+L": "+(st::kKnobs[i].kind==st::Kind::Choice?choice_label(st::kKnobs[i],v[i]):std::to_wstring(v[i]));
  if(launcher::web::action_dialog(g.wnd,L"Review preset",t,{{IDOK,L"Apply changes"},{IDCANCEL,L"Cancel"}})==IDOK){g.st.preset=p;g.st.values=v;rebuild();}
  footer();
}
static void play() {
  if(g_no_game||!g_screenshot.empty()||game_running()||g.stage<4)return;
  if(!save(false)||!valid_game_dir(g.dir))return;
  auto p=make_plan(g.dir,L"",{},false,effective_fg(g.st));DWORD pid=0;std::wstring err,runtime_note;if(!launcher::dlss5::stage(g.st,g.dir,runtime_note,err)){status(err);return;}
  auto guard_note=guard_game_upscaler(g.st);if(!guard_note.empty())runtime_note+=L"\n"+guard_note;
  if(!launch(p,&pid,&err))status(L"Settings saved, but the game could not start. "+err+L" See Help & support. "+runtime_note);
  else status(L"Game started. "+runtime_note);
}
static void command(int id,int code,HWND source) {
  if(g.updating)return;
  if(id>=KNOB&&id<KNOB+st::kKnobCount&&code==CBN_SELCHANGE){
    int i=id-KNOB;const auto& k=st::kKnobs[i];int row=int(SendMessageW(source,CB_GETCURSEL,0,0));int c=int(SendMessageW(source,CB_GETITEMDATA,row,0));
    if(c<0)return;
    if(!st::choice_available(k,c,g.dev,g.experimental,g.st.caps,opt_in())){status(L"This option is unavailable. Check its requirements in Help & support.");int previous=g.scroll;rebuild();scroll_to(previous);SetFocus(control(g.body,KNOB+i));return;}
    g.ab_selection.touch(k,c,opt_in());g.st.values[i]=c;
    // Same policy as the web bridge: the selection turns on what it needs (all or nothing).
    std::vector<int> raised;if(st::raise_needs(g.st.values,i,c,g.dev,g.experimental,g.st.caps,opt_in(),&raised)&&!raised.empty()){
      std::wstring names;for(int j:raised){if(!names.empty())names+=L", ";names+=widen(st::kKnobs[j].label);}status(L"Also turned on: "+names+L". Save to apply.");}
    int previous=g.scroll;rebuild();scroll_to(previous);SetFocus(control(g.body,KNOB+i));return;
  }
  if(id>=DETAILS&&id<DETAILS+st::kKnobCount){auto& k=st::kKnobs[id-DETAILS];std::wstring t=(k.dev_only?widen(k.desc)+L"\n\n":L"")+widen(k.details)+L"\n\n"+widen(std::string(k.cost.provenance)=="No qualified measurement available."?"":k.cost.provenance)+L"\n\nApplies: "+widen(st::apply_label(st::transition_apply(k,id-DETAILS<int(g.saved.size())?g.saved[id-DETAILS]:-1,g.st.values[id-DETAILS]),!k.dev_only))+L"\n"+widen(k.apply_note);MessageBoxW(g.wnd,t.c_str(),widen(k.label).c_str(),MB_OK);return;}
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
    case EXPAND:g.experimental=!g.experimental;
      if(!g.dev)for(int i=0;i<st::kKnobCount;++i)if(st::opt_in_knob(st::kKnobs[i])){if(!g.experimental||g.st.values[i]<0)g.st.values[i]=0;}
      rebuild();SetFocus(control(g.body,EXPAND));break;
    case NEXT:{int previous=g.stage;g.stage=std::min(4,g.stage+1);std::wstring err;if(!persist(&err)){g.stage=previous;status(L"Setup progress could not be saved. "+err);}else rebuild();break;}
    case SAVE:save(false);break;case PLAY:play();break;
    case DISCARD:for(int i=0;i<bg3::hotkeys::kCount;++i)g.st.shortcuts[i]=g.st.loaded_shortcuts[i];g.experimental=g.saved_experimental;g.ab_selection.pending=false;g.st.values=g.saved;g.st.preset=g.saved_preset;g.dir=g.saved_dir;g.st.dlss5_runtime_path=g.st.loaded_dlss5_runtime_path;g.st.dlss5_runtime_status=launcher::dlss5::discovery_status(g.st,g.dir);rebuild();break;
    case RESET:select_preset(g.st.preset);break;
    case CLOSE:SendMessageW(g.wnd,WM_CLOSE,0,0);break;
    case BROWSE:browse();break;
    case HK_RT_RESET:case HK_NR_RESET:{int i=id==HK_RT_RESET?0:1;auto value=std::string(bg3::hotkeys::defaults[i]);
      auto error=bg3::hotkeys::validate_change_at(g.st.shortcuts.data(),i,value);
      if(error.empty()){g.st.shortcuts[i]=value;rebuild();}else status(widen(error));break;}
    case NR_BROWSE:g.st.dlss5_runtime_path=launcher::dlss5::browse(g.wnd,g.st.dlss5_runtime_path);g.st.dlss5_runtime_status=launcher::dlss5::discovery_status(g.st,g.dir);status(L"Runtime file selected. Save and restart the game to apply.");rebuild();break;
    case NR_CLEAR:g.st.dlss5_runtime_path.clear();g.st.dlss5_runtime_status=launcher::dlss5::discovery_status(g.st,g.dir);status(L"DLSS 5 will look in the game folder. Save to keep this change.");rebuild();break;
    case NOTICES:{auto path=root_dir()+L"\\THIRD_PARTY.md";if(!file_exists(path))path=root_dir()+L"\\docs\\release\\THIRD_PARTY.md";ShellExecuteW(g.wnd,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);break;}
    case REPORT:report_problem();break;
    case ERROR_DETAILS:if(launcher::web::action_dialog(g.wnd,L"Error details",g.message+L"\n\nOpen a problem report?",{{IDYES,L"Report a problem"},{IDCANCEL,L"Close"}})==IDYES)report_problem();break;
    case RETRY:g.st.have_gpu=false;g.st.caps=0;g.gpu_err.clear();detect_gpu(g.st,&g.gpu_err);g_inspection=inspect_graphics();g_adapter=default_adapter(g_inspection);g.st.caps=assess_compatibility(g_inspection,g_adapter).caps;rebuild();break;
    case LOG:ShellExecuteW(g.wnd,L"open",(root_dir()+L"\\bg3rtx.log").c_str(),nullptr,nullptr,SW_SHOWNORMAL);break;
    case FOLDER:ShellExecuteW(g.wnd,L"open",root_dir().c_str(),nullptr,nullptr,SW_SHOWNORMAL);break;
  }
}
static LRESULT CALLBACK bodyproc(HWND h,UINT m,WPARAM w,LPARAM l) {
  switch(m){
    case WM_COMMAND:command(LOWORD(w),HIWORD(w),(HWND)l);return 0;
    case WM_NOTIFY:{auto* note=reinterpret_cast<NMHDR*>(l);if(note->idFrom==NOTICES&&(note->code==NM_CLICK||note->code==NM_RETURN))command(NOTICES,BN_CLICKED,note->hwndFrom);return 0;}
    case WM_HSCROLL:{HWND c=(HWND)l;int i=GetDlgCtrlID(c)-KNOB;if(i>=0&&i<st::kKnobCount){auto& k=st::kKnobs[i];int p=int(SendMessageW(c,TBM_GETPOS,0,0));g.st.values[i]=k.slider.has_off&&p<k.slider.min?0:std::clamp(k.slider.min+((p-k.slider.min+k.slider.step/2)/k.slider.step)*k.slider.step,k.slider.min,k.slider.max);footer();if(LOWORD(w)==TB_ENDTRACK){int previous=g.scroll;rebuild();scroll_to(previous);SetFocus(control(g.body,KNOB+i));}}return 0;}
    case WM_VSCROLL:{SCROLLINFO si{sizeof si,SIF_ALL};GetScrollInfo(h,SB_VERT,&si);int n=g.scroll;switch(LOWORD(w)){case SB_LINEUP:n-=S(40);break;case SB_LINEDOWN:n+=S(40);break;case SB_PAGEUP:n-=si.nPage;break;case SB_PAGEDOWN:n+=si.nPage;break;case SB_THUMBTRACK:n=si.nTrackPos;break;}scroll_to(n);return 0;}
    case WM_MOUSEWHEEL:scroll_to(g.scroll-GET_WHEEL_DELTA_WPARAM(w)*S(96)/WHEEL_DELTA);return 0;
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
      if(session!=g.session){g.session=session;g.baseline=saved_settings();g.pending=false;std::wstring err;if(!persist(&err))g.message=L"Restart information could not be saved. "+err;}
      footer();return 0;}
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:SetTextColor((HDC)w,foreground());SetBkColor((HDC)w,background());return LRESULT(g.bg);
    case WM_CLOSE:
      if(dirty_count()){int r=launcher::web::action_dialog(h,L"Unsaved changes",L"Save changes before closing?",{{IDYES,L"Save"},{IDNO,L"Discard changes"},{IDCANCEL,L"Keep editing"}});if(r==IDCANCEL||(r==IDYES&&!save(false)))return 0;}
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
  g_inspection=inspect_graphics();g_adapter=default_adapter(g_inspection);g.st.caps=assess_compatibility(g_inspection,g_adapter).caps;
  g.experimental=st::map_int(saved_settings(),"experimental_options",0)==1;
  g.saved_experimental=g.experimental;
  g.st.values=st::presentation_values(saved_settings(),g.dev,opt_in());g.saved=g.st.values;g.saved_preset=g.st.preset;
  g.dir=game_arg.empty()?saved_game_dir():game_arg;if(!valid_game_dir(g.dir)&&g_screenshot.empty())g.dir=detect_game_dir();g.saved_dir=g.dir;
  std::string state;read_file(ui_path(),state);auto config=st::parse_ini(state);g.stage=std::clamp(std::atoi(st::lookup(config,"onboarding").c_str()),0,4);
  if(g.stage<4&&game_arg.empty()&&!st::lookup(config,"onboarding_game").empty())g.dir=widen(st::lookup(config,"onboarding_game"));
  if(g_screenshot.empty())g.st.dlss5_runtime_status=launcher::dlss5::discovery_status(g.st,g.dir);
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
  if(g_review==L"error")g.message=L"Settings saved, but the game could not start. The selected folder is unavailable or access was denied. Choose a folder in Overview, retry, or use Support > Report a problem. Windows error: access denied (5). Your saved settings are kept.";
  WNDCLASSEXW wc{sizeof wc};wc.lpfnWndProc=wndproc;wc.hInstance=inst;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=g.bg;wc.lpszClassName=L"BG3RTXLauncher";
  wc.hIcon=(HICON)LoadImageW(inst,MAKEINTRESOURCEW(1),IMAGE_ICON,S(32),S(32),0);wc.hIconSm=wc.hIcon;RegisterClassExW(&wc);
  wc.lpfnWndProc=bodyproc;wc.lpszClassName=L"BG3RTXBody";RegisterClassExW(&wc);
  g.wnd=CreateWindowExW(WS_EX_CONTROLPARENT,L"BG3RTXLauncher",L"BG3RTX settings",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,S(g_review_width?g_review_width+16:960),S(900),nullptr,nullptr,inst,nullptr);
  auto emblem=add(g.wnd,L"STATIC",L"",SS_ICON,0);SendMessageW(emblem,STM_SETICON,WPARAM(wc.hIcon),0);SetWindowSubclass(emblem,emblemproc,1,0);place(emblem,16,8,32,32);
  place(add(g.wnd,L"STATIC",L"",SS_LEFT,HEADER,true),64,8,880,40);label(g.wnd,L"Preset",16,60,72,24);
  for(int p=0;p<st::kPresetCount;++p)add(g.wnd,L"BUTTON",widen(st::kPresetLabel[p]),WS_TABSTOP|BS_AUTORADIOBUTTON|BS_PUSHLIKE|(p==0?WS_GROUP:0),PRESET_LOW+p);
  add(g.wnd,L"BUTTON",L"Use suggested preset",WS_TABSTOP,SUGGEST);add(g.wnd,L"STATIC",L"Suggested preset\nTested GPU: RTX 5090",SS_LEFT,NOTE);
  auto tabs=add(g.wnd,WC_TABCONTROLW,L"",WS_TABSTOP,TABS);
  for(auto entry:{std::pair<int,const wchar_t*>{0,L"Overview"},{1,L"Lighting"},{6,L"Surfaces"},{2,L"Performance"},{7,L"DLSS 5"},{3,L"Advanced"},{4,L"Help && support"},{5,L"Developer"}}){
    if(entry.first==5&&!g.dev)continue;
    TCITEMW ti{};ti.mask=TCIF_TEXT;ti.pszText=const_cast<LPWSTR>(entry.second);int row=TabCtrl_GetItemCount(tabs);TabCtrl_InsertItem(tabs,row,&ti);g_tab_ids.push_back(entry.first);if(entry.first==g.tab)TabCtrl_SetCurSel(tabs,row);
  }
  g.body=CreateWindowExW(WS_EX_CONTROLPARENT,L"BG3RTXBody",L"Settings section",WS_CHILD|WS_VISIBLE|WS_VSCROLL,0,0,0,0,g.wnd,nullptr,inst,nullptr);
  add(g.wnd,L"STATIC",L"",SS_LEFT,STATUS);add(g.wnd,L"BUTTON",L"View error details",WS_TABSTOP,ERROR_DETAILS);for(auto p:{std::pair<int,const wchar_t*>{RESET,L"Restore preset settings"},{DISCARD,L"Discard changes"},{SAVE,L"Save"},{PLAY,L"Play"},{CLOSE,L"Close"}})add(g.wnd,L"BUTTON",p.second,WS_TABSTOP,p.first);
  EnableWindow(control(g.wnd,SUGGEST),g.st.have_gpu);
  if(!g.st.have_gpu)SetWindowTextW(control(g.wnd,NOTE),L"Compatibility not checked\nRecheck graphics in Help & support");
  layout();
  if(g_screenshot.empty()){std::wstring err;if(!persist(&err))status(L"Setup information could not be saved. "+err);}
  if(!g_screenshot.empty()){
    SetWindowPos(g.wnd,nullptr,-20000,-20000,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);ShowWindow(g.wnd,SW_SHOWNOACTIVATE);if(g_screenshot_bottom)scroll_to(g.height);
    RedrawWindow(g.wnd,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);screenshot(g_screenshot);DestroyWindow(g.wnd);
  }else{SetTimer(g.wnd,1,2000,nullptr);ShowWindow(g.wnd,SW_SHOW);MSG m;while(GetMessageW(&m,nullptr,0,0)>0){if(IsDialogMessageW(g.wnd,&m))continue;TranslateMessage(&m);DispatchMessageW(&m);}}
  DeleteObject(g.font);DeleteObject(g.bold);DeleteObject(g.bg);RemoveFontResourceExW(font_path.c_str(),FR_PRIVATE,nullptr);RemoveFontResourceExW(semibold_path.c_str(),FR_PRIVATE,nullptr);CoUninitialize();return 0;
}
