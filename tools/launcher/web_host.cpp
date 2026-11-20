#include "web_host.h"
#include "web_bridge.h"
#include "ui_state.h"
#include "session_logging.h"
#include "version.h"
#include "dlss5_runtime.h"
#include "preparation.h"
#include <wrl.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <objbase.h>
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <tlhelp32.h>
#include <filesystem>
#include <memory>
#include <thread>
#include <atomic>
#include <commctrl.h>
#include <shellapi.h>
#pragma comment(lib, "dwmapi.lib")

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;
namespace launcher::web {
int action_dialog(HWND owner, const std::wstring& title, const std::wstring& text,
                  const std::vector<std::pair<int, std::wstring>>& actions) {
  std::vector<TASKDIALOG_BUTTON> buttons;
  for (const auto& action : actions) buttons.push_back({action.first, action.second.c_str()});
  TASKDIALOGCONFIG config{sizeof config};
  config.hwndParent = owner; config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
  config.pszWindowTitle = title.c_str(); config.pszContent = text.c_str();
  config.cButtons = UINT(buttons.size()); config.pButtons = buttons.data();
  config.nDefaultButton = IDCANCEL;
  int selected = IDCANCEL;
  TaskDialogIndirect(&config, &selected, nullptr, nullptr);
  return selected;
}
namespace {
namespace st = bg3::settings;
constexpr UINT work_done = WM_APP + 32;
constexpr UINT work_progress = WM_APP + 33;
struct Host {
  HWND window{};
  ComPtr<ICoreWebView2Controller> controller;
  ComPtr<ICoreWebView2Environment> environment;
  ComPtr<ICoreWebView2> view;
  State state;
  std::unique_ptr<Bridge> bridge;
  std::wstring game, shot, shot_view, export_path;
  std::string session;
  st::Map baseline;
  bool no_game{}, report{}, failed{}, ready{}, busy{}, closed{}, shot_bottom{};
  bool hidden{};            // minimised: the WebView is hidden and suspended, and no state is sent to it
  std::string last_state;   // the last message sent to the page: the 2 s refresh sends only what changed
  float shot_scale{};
  int result{};
  std::thread worker;
  std::atomic<bool> cancel{false};
  std::string reply, after_work;
  std::unique_ptr<prep::Manager> prep;   // Preparation card: the hair conversion and shader compile children
  // Frameless window chrome (see window_proc): logical-pixel metrics scale with the window DPI.
  int dpi=96, hover=HTNOWHERE, pressed=HTNOWHERE;
  bool active=true;
  HFONT caption_font{}, glyph_font{};
  HICON caption_icon{};
  ~Host() { cancel=true; if(worker.joinable())worker.join(); if(controller)controller->Close();
    if(caption_font)DeleteObject(caption_font); if(glyph_font)DeleteObject(glyph_font); if(caption_icon)DestroyIcon(caption_icon); }
};
std::string session_token() {
  HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
  if(snap==INVALID_HANDLE_VALUE)return {};
  PROCESSENTRY32W pe{sizeof pe}; std::string token;
  if(Process32FirstW(snap,&pe))do {
    if(_wcsicmp(pe.szExeFile,L"bg3.exe"))continue;
    HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pe.th32ProcessID);
    FILETIME start{},end{},kernel{},user{};
    if(p){if(GetProcessTimes(p,&start,&end,&kernel,&user))token=std::to_string(pe.th32ProcessID)+":"+std::to_string(start.dwHighDateTime)+":"+std::to_string(start.dwLowDateTime);CloseHandle(p);}break;
  }while(Process32NextW(snap,&pe));CloseHandle(snap);return token;
}
st::Map effective(const State& state) {auto m=state.base;st::apply_layer(m,state.user);return m;}
std::wstring choose_zip(HWND owner) {
  wchar_t path[32768]=L"BG3RTX-report.zip";
  PWSTR docs{};std::wstring folder;
  if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents,0,nullptr,&docs))){folder=docs;CoTaskMemFree(docs);}
  OPENFILENAMEW f{sizeof f};f.hwndOwner=owner;f.lpstrFile=path;f.nMaxFile=32768;
  f.lpstrFilter=L"ZIP report (*.zip)\0*.zip\0";f.lpstrDefExt=L"zip";
  f.lpstrInitialDir=folder.c_str();f.Flags=OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER;
  return GetSaveFileNameW(&f)?std::wstring(path):std::wstring();
}
// File picker for the player's granny2.dll; "" when cancelled.
std::wstring browse_granny(HWND owner,const std::wstring& current) {
  ComPtr<IFileOpenDialog> dialog;
  if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return {};
  const COMDLG_FILTERSPEC filter[]={{L"granny2.dll",L"granny2.dll"}};dialog->SetFileTypes(1,filter);
  dialog->SetOptions(FOS_FORCEFILESYSTEM|FOS_FILEMUSTEXIST|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR);
  dialog->SetTitle(L"Choose granny2.dll from your LSLib ExportTool folder");
  if(!current.empty()){auto dir=current.substr(0,current.find_last_of(L"\\/"));ComPtr<IShellItem> folder;if(SUCCEEDED(SHCreateItemFromParsingName(dir.c_str(),nullptr,IID_PPV_ARGS(&folder))))dialog->SetFolder(folder.Get());}
  std::wstring out;
  if(SUCCEEDED(dialog->Show(owner))){ComPtr<IShellItem> item;if(SUCCEEDED(dialog->GetResult(&item))){PWSTR path{};if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){out=path;CoTaskMemFree(path);}}}
  return out;
}
void send(Host& h,const std::string& json) {if(h.view){h.last_state=json;h.view->PostWebMessageAsJson(widen(json).c_str());}}
// Every 2 s (timer 1). The page rebuilds itself from each state it receives, so an unchanged state is not sent again:
// while the game runs the launcher then costs two process snapshots and a few file checks per tick, and no WebView work.
void refresh(Host& h) {
  if(h.busy||!h.bridge)return;
  auto session=session_token();
  if(session!=h.session){h.session=session;h.baseline=effective(h.state);h.bridge->set_restart({});}
  h.bridge->set_mod_status(file_exists(exe_dir()+L"\\VkLayer_bg3rtx.dll")&&file_exists(exe_dir()+L"\\bg3rtx_core.dll")?"Mod files present":"Mod files missing");
  const bool playing=game_running();
  h.bridge->set_can_play(!h.no_game&&valid_game_dir(h.game)&&!playing);
  if(h.prep)h.prep->tick(playing);
  if(h.hidden)return;
  auto json=h.bridge->state_json();
  if(json!=h.last_state)send(h,json);
}
void fail(Host& h, const char* stage="WebView2 initialization", HRESULT error=E_FAIL) {
  if(!h.shot.empty())write_file_atomic(h.shot+L".error.txt",std::string(stage)+": HRESULT "+std::to_string(static_cast<unsigned long>(error)),nullptr);
  h.failed=true;DestroyWindow(h.window);
}
bool allowed(const wchar_t* uri) {
  return uri && (wcsncmp(uri,L"https://app.bg3rtx.local/",25)==0 ||
    wcsncmp(uri,L"https://fonts.bg3rtx.local/",27)==0 ||
    wcsncmp(uri,L"https://branding.bg3rtx.local/",30)==0);
}
void capture(Host& h) {
  KillTimer(h.window,2);ComPtr<IStream> stream;
  HRESULT hr=SHCreateStreamOnFileEx(h.shot.c_str(),STGM_CREATE|STGM_WRITE|STGM_SHARE_EXCLUSIVE,FILE_ATTRIBUTE_NORMAL,TRUE,nullptr,&stream);
  if(FAILED(hr)){h.result=2;DestroyWindow(h.window);return;}
  h.view->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG,stream.Get(),
    Callback<ICoreWebView2CapturePreviewCompletedHandler>([&h,stream](HRESULT hr)->HRESULT {
      h.result=SUCCEEDED(hr)?0:2;DestroyWindow(h.window);return S_OK;
    }).Get());
}
// ---- Frameless window chrome ----
// WM_NCCALCSIZE removes only the caption and top frame. The left, right and bottom
// frames stay native (invisible resize borders, DWM shadow, rounded corners on
// Windows 11). The parent paints a slim title bar above the WebView and answers
// WM_NCHITTEST with caption, top-edge and caption-button codes, so dragging, Aero
// snap, double-click maximise, the Windows 11 snap layouts flyout (HTMAXBUTTON) and
// the system menu (right click, icon click, Alt+Space) behave as on a standard window.
constexpr COLORREF kCaptionBg=RGB(0x13,0x15,0x18),kCaptionLine=RGB(0x2C,0x30,0x37),kCaptionText=RGB(0xF1,0xE7,0xD2),
  kCaptionMuted=RGB(0xB2,0xAA,0x9A),kCaptionHover=RGB(0x2A,0x2E,0x35),kCaptionPressed=RGB(0x36,0x3B,0x43),
  kCloseHover=RGB(0xC4,0x2B,0x1C),kClosePressed=RGB(0x9B,0x23,0x17);
int px(const Host& h,int v){return MulDiv(v,h.dpi,96);}
int caption_height(const Host& h){return px(h,32);}
bool caption_button(WPARAM code){return code==HTMINBUTTON||code==HTMAXBUTTON||code==HTCLOSE;}
RECT caption_button_rect(const Host& h,int code){
  RECT r{};GetClientRect(h.window,&r);int width=px(h,46),index=code==HTCLOSE?0:code==HTMAXBUTTON?1:2;
  r.right-=width*index;r.left=r.right-width;r.top=0;r.bottom=caption_height(h);return r;
}
RECT webview_bounds(const Host& h){RECT r{};GetClientRect(h.window,&r);r.top=std::min<LONG>(r.bottom,caption_height(h));return r;}
void invalidate_caption(const Host& h){RECT r{};GetClientRect(h.window,&r);r.bottom=caption_height(h);InvalidateRect(h.window,&r,FALSE);}
bool font_installed(const wchar_t* face){
  HDC dc=GetDC(nullptr);LOGFONTW font{};font.lfCharSet=DEFAULT_CHARSET;wcsncpy_s(font.lfFaceName,face,_TRUNCATE);bool found=false;
  EnumFontFamiliesExW(dc,&font,[](const LOGFONTW*,const TEXTMETRICW*,DWORD,LPARAM found)->int{*reinterpret_cast<bool*>(found)=true;return 0;},LPARAM(&found),0);
  ReleaseDC(nullptr,dc);return found;
}
void chrome_resources(Host& h){
  if(h.caption_font)DeleteObject(h.caption_font);if(h.glyph_font)DeleteObject(h.glyph_font);if(h.caption_icon)DestroyIcon(h.caption_icon);h.caption_icon=nullptr;
  h.caption_font=CreateFontW(-px(h,12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  // Windows 11 caption glyphs; Windows 10 ships the same code points in Segoe MDL2 Assets.
  static const bool fluent=font_installed(L"Segoe Fluent Icons");
  h.glyph_font=CreateFontW(-px(h,10),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,fluent?L"Segoe Fluent Icons":L"Segoe MDL2 Assets");
  LoadIconWithScaleDown(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(1),px(h,16),px(h,16),&h.caption_icon);
}
void paint_caption(const Host& h,HDC target){
  RECT client{};GetClientRect(h.window,&client);RECT bar{0,0,client.right,caption_height(h)};
  if(bar.right<=0||bar.bottom<=0)return;
  HDC dc=CreateCompatibleDC(target);HBITMAP bitmap=CreateCompatibleBitmap(target,bar.right,bar.bottom);HGDIOBJ old_bitmap=SelectObject(dc,bitmap);
  auto fill=[dc](RECT r,COLORREF colour){HBRUSH brush=CreateSolidBrush(colour);FillRect(dc,&r,brush);DeleteObject(brush);};
  fill(bar,kCaptionBg);fill({0,bar.bottom-std::max(1,px(h,1)),bar.right,bar.bottom},kCaptionLine);
  int x=px(h,12),icon=px(h,16);
  if(h.caption_icon)DrawIconEx(dc,x,(bar.bottom-icon)/2,h.caption_icon,icon,icon,0,nullptr,DI_NORMAL);
  x+=icon+px(h,10);
  SetBkMode(dc,TRANSPARENT);HGDIOBJ old_font=SelectObject(dc,h.caption_font);
  const std::wstring name=L"BG3RTX",version=widen(product_version());
  RECT text{x,0,caption_button_rect(h,HTMINBUTTON).left-px(h,8),bar.bottom};
  SetTextColor(dc,h.active?kCaptionText:kCaptionMuted);DrawTextW(dc,name.c_str(),int(name.size()),&text,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS);
  SIZE extent{};GetTextExtentPoint32W(dc,name.c_str(),int(name.size()),&extent);text.left+=extent.cx+px(h,8);
  SetTextColor(dc,kCaptionMuted);if(text.left<text.right)DrawTextW(dc,version.c_str(),int(version.size()),&text,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS);
  SelectObject(dc,h.glyph_font);
  for(int code:{HTMINBUTTON,HTMAXBUTTON,HTCLOSE}){
    RECT r=caption_button_rect(h,code);r.bottom-=std::max(1,px(h,1));const bool hot=h.hover==code,down=hot&&h.pressed==code;
    if(hot)fill(r,code==HTCLOSE?(down?kClosePressed:kCloseHover):(down?kCaptionPressed:kCaptionHover));
    SetTextColor(dc,code==HTCLOSE&&hot?RGB(255,255,255):h.active?kCaptionText:kCaptionMuted);
    const wchar_t* glyph=code==HTCLOSE?L"\xE8BB":code==HTMINBUTTON?L"\xE921":IsZoomed(h.window)?L"\xE923":L"\xE922";
    DrawTextW(dc,glyph,1,&r,DT_SINGLELINE|DT_CENTER|DT_VCENTER|DT_NOPREFIX);
  }
  BitBlt(target,0,0,bar.right,bar.bottom,dc,0,0,SRCCOPY);
  SelectObject(dc,old_font);SelectObject(dc,old_bitmap);DeleteObject(bitmap);DeleteDC(dc);
}
// Client-area part of the hit test: the painted title bar. Returns HTCLIENT below it.
LRESULT caption_hit(const Host& h,LPARAM l){
  POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h.window,&p);
  RECT client{};GetClientRect(h.window,&client);
  if(p.y<0||p.y>=caption_height(h)||p.x<0||p.x>=client.right)return HTCLIENT;
  const int edge=GetSystemMetricsForDpi(SM_CYSIZEFRAME,h.dpi);
  if(!IsZoomed(h.window)&&p.y<edge)return p.x<edge*2?HTTOPLEFT:p.x>=client.right-edge*2?HTTOPRIGHT:HTTOP;
  for(int code:{HTCLOSE,HTMAXBUTTON,HTMINBUTTON}){RECT r=caption_button_rect(h,code);if(PtInRect(&r,p))return code;}
  return p.x<px(h,40)?HTSYSMENU:HTCAPTION;
}
void system_menu(const Host& h,int x,int y){
  HMENU menu=GetSystemMenu(h.window,FALSE);if(!menu)return;const bool zoomed=IsZoomed(h.window);
  auto enable=[menu](UINT item,bool on){EnableMenuItem(menu,item,MF_BYCOMMAND|(on?MF_ENABLED:MF_GRAYED));};
  enable(SC_RESTORE,zoomed);enable(SC_MOVE,!zoomed);enable(SC_SIZE,!zoomed);enable(SC_MINIMIZE,true);enable(SC_MAXIMIZE,!zoomed);enable(SC_CLOSE,true);
  SetMenuDefaultItem(menu,UINT(-1),FALSE);
  int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON|(GetSystemMetrics(SM_MENUDROPALIGNMENT)?TPM_RIGHTALIGN:TPM_LEFTALIGN),x,y,0,h.window,nullptr);
  if(command)PostMessageW(h.window,WM_SYSCOMMAND,WPARAM(command),0);
}
void apply_window_frame(Host& h){
  h.dpi=GetDpiForWindow(h.window);chrome_resources(h);
  const BOOL dark=TRUE;DwmSetWindowAttribute(h.window,20/*DWMWA_USE_IMMERSIVE_DARK_MODE*/,&dark,sizeof dark);
  const int round=2/*DWMWCP_ROUND*/;DwmSetWindowAttribute(h.window,33/*DWMWA_WINDOW_CORNER_PREFERENCE*/,&round,sizeof round);
  const COLORREF border=RGB(0x3A,0x3F,0x47);DwmSetWindowAttribute(h.window,34/*DWMWA_BORDER_COLOR*/,&border,sizeof border);
  SetWindowPos(h.window,nullptr,0,0,0,0,SWP_FRAMECHANGED|SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
}
LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM w,LPARAM l) {
  auto* h=reinterpret_cast<Host*>(GetWindowLongPtrW(window,GWLP_USERDATA));
  if(message==WM_NCCREATE){h=static_cast<Host*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);h->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,LONG_PTR(h));}
  if(!h)return DefWindowProcW(window,message,w,l);
  switch(message){
    case WM_NCCALCSIZE:if(w){
        auto* p=reinterpret_cast<NCCALCSIZE_PARAMS*>(l);const LONG top=p->rgrc[0].top;
        LRESULT r=DefWindowProcW(window,message,w,l);if(r)return r;
        p->rgrc[0].top=top;  // keep the native left, right and bottom frame
        if(IsZoomed(window)){UINT dpi=GetDpiForWindow(window);p->rgrc[0].top+=GetSystemMetricsForDpi(SM_CYFRAME,dpi)+GetSystemMetricsForDpi(SM_CXPADDEDBORDER,dpi);}
        return 0;
      }break;
    case WM_NCHITTEST:{LRESULT r=DefWindowProcW(window,message,w,l);return r==HTCLIENT?caption_hit(*h,l):r;}
    case WM_NCMOUSEMOVE:{
        int hot=caption_button(w)?int(w):HTNOWHERE;if(hot!=h->hover){h->hover=hot;if(hot==HTNOWHERE)h->pressed=HTNOWHERE;invalidate_caption(*h);}
        if(hot!=HTNOWHERE){TRACKMOUSEEVENT track{sizeof track,TME_LEAVE|TME_NONCLIENT,window,0};TrackMouseEvent(&track);}
      }break;
    case WM_NCMOUSELEAVE:h->hover=h->pressed=HTNOWHERE;invalidate_caption(*h);return 0;
    case WM_NCLBUTTONDOWN:if(caption_button(w)){h->pressed=int(w);invalidate_caption(*h);return 0;}break;
    case WM_NCLBUTTONDBLCLK:if(caption_button(w))return 0;break;
    case WM_NCLBUTTONUP:if(caption_button(w)){
        const bool clicked=h->pressed==int(w);h->pressed=HTNOWHERE;invalidate_caption(*h);
        if(clicked)PostMessageW(window,WM_SYSCOMMAND,w==HTMINBUTTON?SC_MINIMIZE:w==HTCLOSE?SC_CLOSE:IsZoomed(window)?SC_RESTORE:SC_MAXIMIZE,0);
        return 0;
      }break;
    case WM_NCRBUTTONUP:if(w==HTCAPTION||w==HTSYSMENU){system_menu(*h,GET_X_LPARAM(l),GET_Y_LPARAM(l));return 0;}break;
    case WM_ACTIVATE:h->active=LOWORD(w)!=WA_INACTIVE;invalidate_caption(*h);break;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps{};HDC dc=BeginPaint(window,&ps);paint_caption(*h,dc);EndPaint(window,&ps);return 0;}
    case WM_DPICHANGED:{
        h->dpi=HIWORD(w);chrome_resources(*h);const RECT* r=reinterpret_cast<const RECT*>(l);
        SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);
        invalidate_caption(*h);return 0;
      }
    case WM_SETFOCUS:if(h->controller)h->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);return 0;
    case WM_SIZE:
      if(h->controller){
        // Minimised (the usual state while the game runs): hide the WebView and let it suspend, which stops its timers
        // and rendering and lets the browser processes trim memory. Showing it again resumes it; the state is sent anew.
        const bool hidden=w==SIZE_MINIMIZED;
        if(hidden!=h->hidden){
          h->hidden=hidden;h->controller->put_IsVisible(hidden?FALSE:TRUE);
          if(hidden){ComPtr<ICoreWebView2_3> view3;if(h->view&&SUCCEEDED(h->view.As(&view3)))view3->TrySuspend(Callback<ICoreWebView2TrySuspendCompletedHandler>([](HRESULT,BOOL)->HRESULT{return S_OK;}).Get());}
          else if(h->ready&&h->shot.empty()){h->last_state.clear();refresh(*h);}
        }
        if(!hidden)h->controller->put_Bounds(webview_bounds(*h));
      }
      invalidate_caption(*h);return 0;
    case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={px(*h,736),px(*h,620)};return 0;
    // Timer 3: the page did not load within a minute. A review shot fails; the player's window falls back to the native
    // launcher (run() returns -1) instead of closing without any window.
    case WM_TIMER:if(w==2)capture(*h);else if(w==3){h->result=2;h->failed=h->shot.empty();DestroyWindow(window);}else refresh(*h);return 0;
    case work_done:if(h->worker.joinable())h->worker.join();h->busy=false;
      if(!h->after_work.empty()){h->reply=h->bridge->handle(h->after_work);h->after_work.clear();}
      send(*h,h->reply);return 0;
    case work_progress:send(*h,"{\"type\":\"progress\",\"done\":"+std::to_string(w)+",\"total\":"+std::to_string(l)+"}");return 0;
    case WM_CLOSE:
      if(h->busy){h->cancel=true;return 0;}
      // Every change is already in the file (instant apply): nothing to ask.
      DestroyWindow(window);return 0;
    case WM_DESTROY:h->closed=true;KillTimer(window,1);KillTimer(window,2);KillTimer(window,3);PostQuitMessage(0);return 0;
  }return DefWindowProcW(window,message,w,l);
}
}

// Screenshot review: the real settings table and copy, Ultra defaults and a reference RTX 5090 without
// local groom files. Nothing is read from or written to the player's files; no hooks, so no side effects.
void review_bridge(Host& h,const std::wstring& view){
  h.state.base=st::code_defaults();h.state.base["dev"]=view==L"advanced"?"1":"0";
  h.state.preset=st::kUltra;h.state.values=st::preset_values(st::kUltra);h.state.loaded=h.state.values;
  if(view==L"needs")h.state.values[st::knob_index("rt_geometry")]=0;
  h.bridge=std::make_unique<Bridge>(h.state);
  GraphicsInspection probe;GraphicsAdapter a;a.gpu.vendor=0x10DE;a.gpu.device=0x2B85;a.gpu.name="NVIDIA GeForce RTX 5090";
  a.gpu.ray_query=a.gpu.discrete=a.gpu.opacity_micromap=a.gpu.linear_swept_spheres=a.gpu.shader_int64=a.gpu.fragment_stores=true;
  a.driver_version="621.01";a.dedicated_bytes=uint64_t(32)<<30;probe.adapters.push_back(a);
  if(view==L"probe"){probe.adapters.push_back(a);probe.adapters.back().gpu.name="Integrated graphics adapter";h.bridge->set_compatibility(probe,-1);}
  else h.bridge->set_compatibility(probe);
  h.bridge->set_version(product_version());h.bridge->set_mod_status("Mod files present");h.bridge->set_game_path(view==L"missing"?"":"C:\\Games\\Baldurs Gate 3");
  if(view==L"experimental")h.bridge->handle("{\"type\":\"experimental\",\"value\":true}");
  if(view==L"preset")h.bridge->handle("{\"type\":\"preset\",\"value\":2}");
  if(view==L"report")h.bridge->handle("{\"type\":\"report\"}");
  h.bridge->set_status("");   // the page shows a missing game folder itself
}
int run(HINSTANCE inst,const std::wstring& game,bool report,bool no_game,const std::wstring& screenshot,int width,const std::wstring& shot_view,float shot_scale,bool shot_bottom) {
  LPWSTR version{};
  if(FAILED(GetAvailableCoreWebView2BrowserVersionString(nullptr,&version)))return screenshot.empty()?-1:2;
  CoTaskMemFree(version);
  std::wstring ui=exe_dir()+L"\\ui";
  if(!file_exists(ui+L"\\index.html"))return screenshot.empty()?-1:2;
  HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  if(FAILED(com))return -1;
  // Per-monitor v2 for this window only: the title bar and WebView follow monitor DPI changes.
  // The native fallback (ui_product.h) keeps the process's system-DPI scaling.
  const DPI_AWARENESS_CONTEXT previous_dpi=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  int rc=0;
  {
    auto owner=std::make_shared<Host>();
    Host& h=*owner;h.no_game=no_game||report||!screenshot.empty();h.report=report;h.result=report?3:0;h.shot=screenshot;h.shot_view=shot_view;h.shot_scale=shot_scale;h.shot_bottom=shot_bottom;
    h.game=!screenshot.empty()?L"":(game.empty()?saved_game_dir():game);
    if(!h.no_game&&!valid_game_dir(h.game))h.game=detect_game_dir();
    // Screenshot mode does not read player configuration or probe hardware.
    if(screenshot.empty()){
      load_state(h.state);h.baseline=effective(h.state);h.session=session_token();
      std::string text;read_file(root_dir()+L"\\bg3rtx_ui.ini",text);auto saved=st::parse_ini(text);
      bool pending=ui_state::same_session(st::lookup(saved,"session"),h.session)&&st::lookup(saved,"pending_restart")=="1";
      if(pending)for(auto& kv:saved)if(kv.first.rfind("baseline_",0)==0)h.baseline[kv.first.substr(9)]=kv.second;
      Hooks hooks;
      hooks.save=[&h](State& candidate,const st::KVList& required,std::string& error){
        if(!candidate.settings_readable){error="Settings could not be read. Close and retry before saving.";return false;}
        if(h.no_game){error="Review mode cannot write settings.";return false;}
        auto forced=forced_keys(candidate);forced.insert(forced.end(),required.begin(),required.end());
        auto text=st::render_user_ini(candidate.user_text,candidate.preset,candidate.values,forced,st::layers_dev(candidate.base,st::parse_ini(candidate.user_text)),&candidate.loaded);
        auto after=candidate.base;st::apply_layer(after,st::parse_ini(text));
        bool pending=game_running()&&ui_state::restart(h.baseline,after);std::wstring err;
        if(!write_file_atomic(root_dir()+L"\\bg3rtx_ui.ini",ui_state::serialize(4,pending,h.session,h.baseline),&err)){error="Settings not saved. Restart information could not be saved.\n"+narrow(err);return false;}
        if(!write_file_atomic(user_ini_path(),text,&err)){error="Settings not saved. Nothing was changed.\n"+narrow(err);return false;}
        candidate.user_text=text;candidate.user=st::parse_ini(text);candidate.loaded=candidate.values;candidate.loaded_dlss5_runtime_path=candidate.dlss5_runtime_path;
        {int others=0;auto names=pending?ui_state::restart_names(h.baseline,after,&others):std::vector<std::string>{};h.bridge->set_restart(std::move(names),pending?others:0);}return true;
      };
      hooks.play=[&h](std::string& error){
        if(h.no_game||game_running()||!valid_game_dir(h.game)){error="Choose the game folder or close the running game to enable Play.";return false;}
        DWORD pid{};std::wstring err,note;if(!launcher::dlss5::stage(h.state,h.game,note,err)){error=narrow(err);return false;}
        auto guard_note=guard_game_upscaler(h.state);if(!guard_note.empty())note+=L"\n"+guard_note;
        bool ok=launch(make_plan(h.game,L"",{},false,effective_fg(h.state)),&pid,&err);
        // The game is starting: stop the shader precompile and (unless "Also while playing") the hair converter now,
        // not at the next 2 s tick, so they do not compete with the level load.
        if(ok&&h.prep)h.prep->tick(true);error=ok?(std::string("Game started. ")+narrow(note)):std::string("The game could not start. See Help & support.\n")+narrow(err)+"\n"+narrow(note);return ok;
      };
      hooks.reportHint=[] {return session_logging::hint(root_dir());};
      hooks.prepareReport=[&h](const std::string& issue,bool logs,bool settings,bool system,bool save){
        diagnostics::Request r;r.install_root=root_dir();r.issue=issue;r.include_logs=logs;r.include_settings=settings;r.include_system=system;r.include_save=save;
        if(system)r.system_text=graphics_report(inspect_graphics());
        return diagnostics::prepare_report(r,[&h](size_t done,size_t total){PostMessageW(h.window,work_progress,done,total);return !h.cancel.load();});
      };
      hooks.exportReport=[&h](const diagnostics::Snapshot& snapshot,std::string& error){
        if(h.export_path.empty()){error="Save cancelled. Preview kept.";return false;}
        auto result=diagnostics::save_report_zip(snapshot,h.export_path,[&h](size_t done,size_t total){PostMessageW(h.window,work_progress,done,total);return !h.cancel.load();});if(h.report)h.result=diagnostics::exit_code(result.status);error=result.message;return result.status==diagnostics::Status::Complete||result.status==diagnostics::Status::Partial;
      };
      hooks.browseGame=[&h](){
        if(h.no_game)return narrow(h.game);
        ComPtr<IFileOpenDialog> dialog;
        if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return narrow(h.game);
        dialog->SetOptions(FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR);
        dialog->SetTitle(L"Choose the game folder containing bin\\bg3.exe");
        if(SUCCEEDED(dialog->Show(h.window))){ComPtr<IShellItem> item;if(SUCCEEDED(dialog->GetResult(&item))){PWSTR path{};if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){auto dir=game_dir_from_path(path);if(valid_game_dir(dir)){std::wstring error;if(save_game_dir(dir,&error)){h.game=dir;if(h.prep)h.prep=std::make_unique<prep::Manager>(root_dir(),h.game);}else h.bridge->set_status("Game folder not saved. "+narrow(error));}else h.bridge->set_status("Choose the game folder containing bin\\bg3.exe.");CoTaskMemFree(path);}}}
        h.state.dlss5_runtime_status=launcher::dlss5::discovery_status(h.state,h.game);
        h.bridge->set_can_play(valid_game_dir(h.game)&&!game_running());return narrow(h.game);
      };
      h.state.dlss5_runtime_status=launcher::dlss5::discovery_status(h.state,h.game);
      hooks.dlss5RuntimeStatus=[&h](const std::string& path){State candidate=h.state;candidate.dlss5_runtime_path=path;return launcher::dlss5::discovery_status(candidate,h.game);};
      hooks.browseDlss5Runtime=[&h](){return h.no_game?h.state.dlss5_runtime_path:launcher::dlss5::browse(h.window,h.state.dlss5_runtime_path);};
      hooks.recheck=[](){return inspect_graphics();};
      hooks.notices=[&h](){auto path=root_dir()+L"\\THIRD_PARTY.md";if(!file_exists(path))path=root_dir()+L"\\docs\\release\\THIRD_PARTY.md";ShellExecuteW(h.window,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);};
      if(!h.no_game)h.prep=std::make_unique<prep::Manager>(root_dir(),h.game);
      hooks.preparation=[&h](){return h.prep?h.prep->json():std::string();};
      hooks.prepare=[&h](const std::string& action,int value){
        if(!h.prep)return false;
        // The player's own granny2.dll (LSLib ExportTool) for "Convert all styles": picked here, kept by the Manager.
        if(action=="browseGranny"){
          auto path=browse_granny(h.window,h.prep->granny());
          if(!path.empty()&&path!=h.prep->granny()&&!h.prep->set_granny(path))h.bridge->set_status("Choose the file named granny2.dll.");
          h.prep->tick(game_running());return true;
        }
        if(!h.prep->act(action,value))return false;h.prep->tick(game_running());return true;};
      h.bridge=std::make_unique<Bridge>(h.state,std::move(hooks));h.bridge->set_version(product_version());
      h.bridge->set_game_path(narrow(h.game));
      h.bridge->set_compatibility(inspect_graphics());{int others=0;auto names=pending?ui_state::restart_names(h.baseline,effective(h.state),&others):std::vector<std::string>{};h.bridge->set_restart(std::move(names),pending?others:0);}
      h.bridge->set_status("");   // the page shows a missing game folder itself
    } else review_bridge(h,shot_view);
    WNDCLASSEXW cls{sizeof cls};cls.lpfnWndProc=window_proc;cls.hInstance=inst;cls.lpszClassName=L"BG3RTX.WebView";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    cls.hIcon=LoadIconW(inst,MAKEINTRESOURCEW(1));cls.hIconSm=HICON(LoadImageW(inst,MAKEINTRESOURCEW(1),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),0));
    cls.hbrBackground=CreateSolidBrush(kCaptionBg);RegisterClassExW(&cls);
    // Client size in logical pixels: 32 for the title bar plus the 760 the page was designed for.
    // A review scale stands in for the display scale: physical size and WebView rasterization follow it.
    const UINT system_dpi=!screenshot.empty()&&shot_scale>0?UINT(96*shot_scale+.5f):GetDpiForSystem();RECT r{0,0,MulDiv(width>0?width:1180,system_dpi,96),MulDiv(760+32,system_dpi,96)};
    AdjustWindowRectExForDpi(&r,WS_OVERLAPPEDWINDOW,FALSE,0,system_dpi);r.top+=GetSystemMetricsForDpi(SM_CYCAPTION,system_dpi);
    HWND created=CreateWindowW(cls.lpszClassName,L"BG3RTX",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,nullptr,nullptr,inst,&h);
    if(!created){h.closed=true;h.failed=true;}
    else apply_window_frame(h);
    PWSTR local{};std::wstring profile;
    if(!screenshot.empty())profile=exe_dir()+L"\\webview-review-profile";
    else if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local))){profile=std::wstring(local)+L"\\BG3RTX\\WebView2";CoTaskMemFree(local);}
    if(h.failed||profile.empty()){DestroyWindow(h.window);h.failed=true;}
    else {
      auto options=Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
      options->put_AdditionalBrowserArguments(L"--disable-background-networking --disable-component-update --disable-sync --no-first-run");
      HRESULT started=CreateCoreWebView2EnvironmentWithOptions(nullptr,profile.c_str(),options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([owner,ui](HRESULT result,ICoreWebView2Environment* env)->HRESULT {
          auto& h=*owner;if(h.closed)return S_OK;
          if(FAILED(result)||!env){fail(h,"Environment",result);return S_OK;}
          h.environment=env;
          HRESULT started=env->CreateCoreWebView2Controller(h.window,Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([owner,ui](HRESULT result,ICoreWebView2Controller* controller)->HRESULT {
            auto& h=*owner;if(h.closed){if(controller)controller->Close();return S_OK;}
            if(FAILED(result)||!controller){fail(h,"Controller",result);return S_OK;}h.controller=controller;controller->get_CoreWebView2(&h.view);
            ComPtr<ICoreWebView2_3> mapping;if(FAILED(h.view.As(&mapping))){fail(h);return S_OK;}
            if(FAILED(mapping->SetVirtualHostNameToFolderMapping(L"app.bg3rtx.local",ui.c_str(),COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS))||
              FAILED(mapping->SetVirtualHostNameToFolderMapping(L"fonts.bg3rtx.local",(ui+L"\\fonts").c_str(),COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW))||
              FAILED(mapping->SetVirtualHostNameToFolderMapping(L"branding.bg3rtx.local",(ui+L"\\branding").c_str(),COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW))){fail(h);return S_OK;}
            ComPtr<ICoreWebView2Settings> settings;h.view->get_Settings(&settings);settings->put_AreDevToolsEnabled(FALSE);settings->put_AreDefaultContextMenusEnabled(FALSE);settings->put_AreHostObjectsAllowed(FALSE);
            settings->put_IsStatusBarEnabled(FALSE);
            ComPtr<ICoreWebView2Settings3> settings3;if(SUCCEEDED(settings.As(&settings3)))settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
            EventRegistrationToken token{};
            h.view->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT {LPWSTR uri{};args->get_Uri(&uri);args->put_Cancel(!uri||wcsncmp(uri,L"https://app.bg3rtx.local/",25)!=0);CoTaskMemFree(uri);return S_OK;}).Get(),&token);
            h.view->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>([](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs* args)->HRESULT{args->put_Handled(TRUE);return S_OK;}).Get(),&token);
            h.view->AddWebResourceRequestedFilter(L"*",COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
            h.view->add_WebResourceRequested(Callback<ICoreWebView2WebResourceRequestedEventHandler>([&h](ICoreWebView2*,ICoreWebView2WebResourceRequestedEventArgs* args)->HRESULT {
              ComPtr<ICoreWebView2WebResourceRequest> request;args->get_Request(&request);LPWSTR uri{};request->get_Uri(&uri);
              if(!allowed(uri)){ComPtr<ICoreWebView2WebResourceResponse> response;h.environment->CreateWebResourceResponse(nullptr,403,L"Local UI only",L"Content-Type: text/plain",&response);args->put_Response(response.Get());}CoTaskMemFree(uri);return S_OK;
            }).Get(),&token);
            h.view->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>([](ICoreWebView2*,ICoreWebView2PermissionRequestedEventArgs* args)->HRESULT{args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);return S_OK;}).Get(),&token);
            h.view->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>([&h](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs* args)->HRESULT {
              if(!h.bridge)return S_OK;LPWSTR source{},json{};args->get_Source(&source);
              bool trusted=source&&wcsncmp(source,L"https://app.bg3rtx.local/",25)==0;CoTaskMemFree(source);if(!trusted)return S_OK;
              if(FAILED(args->get_WebMessageAsJson(&json)))return S_OK;auto text=narrow(json);CoTaskMemFree(json);
              if(h.busy){if(Bridge::is_cancel_collection_intent(text)){h.cancel=true;h.after_work=text;}return S_OK;}
              if(Bridge::is_export_intent(text)){
                h.export_path=choose_zip(h.window);
                if(h.export_path.empty()){h.bridge->set_status("Save cancelled. Preview kept.");send(h,h.bridge->state_json());return S_OK;}
              }
              // Only bounded allowlisted collection runs on a worker; mutations stay serialized.
              if(Bridge::is_collection_intent(text)||Bridge::is_export_intent(text)){
                h.busy=true;h.cancel=false;h.worker=std::thread([&h,text]{try{h.reply=h.bridge->handle(text);}catch(...){h.bridge->set_status("Report failed. Preview and settings are kept.");h.reply=h.bridge->state_json();}PostMessageW(h.window,work_done,0,0);});}
              else send(h,h.bridge->handle(text));return S_OK;
            }).Get(),&token);
            h.view->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>([&h](ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs* args)->HRESULT {
              BOOL ok{};args->get_IsSuccess(&ok);if(!ok){COREWEBVIEW2_WEB_ERROR_STATUS error{};args->get_WebErrorStatus(&error);fail(h,"Navigation",HRESULT(error));return S_OK;}h.ready=true;KillTimer(h.window,3);
              if(!h.shot.empty()){if(h.bridge)send(h,h.bridge->state_json());SetTimer(h.window,2,1600,nullptr);}
              else {refresh(h);if(h.report)send(h,h.bridge->handle("{\"type\":\"report\"}"));SetTimer(h.window,1,2000,nullptr);}return S_OK;
            }).Get(),&token);
            controller->put_Bounds(webview_bounds(h));controller->put_IsVisible(TRUE);
            if(!h.shot.empty()&&h.shot_scale>0){ComPtr<ICoreWebView2Controller3> scaled;if(SUCCEEDED(h.controller.As(&scaled))){scaled->put_ShouldDetectMonitorScaleChanges(FALSE);scaled->put_RasterizationScale(h.shot_scale);}}
            std::wstring url=L"https://app.bg3rtx.local/index.html";
            if(!h.shot.empty())url+=L"?sample=1&view="+h.shot_view+(h.shot_bottom?L"&bottom=1":L"");
            h.view->Navigate(url.c_str());return S_OK;
          }).Get());if(FAILED(started))fail(h);return S_OK;
        }).Get());
      if(FAILED(started))fail(h);
    }
    if(!h.failed){ShowWindow(h.window,SW_SHOWNOACTIVATE);SetTimer(h.window,3,60000,nullptr);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    h.closed=true;h.cancel=true;
    if(h.worker.joinable())h.worker.join();
    if(IsWindow(h.window))DestroyWindow(h.window);
    if(h.controller)h.controller->Close();
    h.view.Reset();h.controller.Reset();h.environment.Reset();
    rc=h.failed?(screenshot.empty()?-1:2):h.result;
  }
  if(rc==-1){MSG quit{};PeekMessageW(&quit,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE);}
  if(previous_dpi)SetThreadDpiAwarenessContext(previous_dpi);
  CoUninitialize();return rc;
}

namespace {
constexpr UINT native_done = WM_APP + 40, native_progress = WM_APP + 41;
struct NativeReportPreview {
  HWND window{}, privacy{}, files{}, contents{}, status{}, save{}, cancelButton{};
  diagnostics::Snapshot snapshot;
  diagnostics::ExportResult exported;
  std::thread worker;
  std::atomic<bool> cancel{false};
  bool busy{}, exporting{}, closeWhenDone{}, includeSave{};
  HFONT body{}, mono{};
  ~NativeReportPreview() {
    cancel = true;
    if (worker.joinable()) worker.join();
    if (body) DeleteObject(body);
    if (mono) DeleteObject(mono);
  }
};
void native_layout(NativeReportPreview& p) {
  RECT r{}; GetClientRect(p.window, &r);
  int width = r.right, height = r.bottom;
  int listWidth = width > 900 ? 260 : 210;
  MoveWindow(p.privacy, 16, 12, width - 32, 148, TRUE);
  MoveWindow(p.files, 16, 168, listWidth, height - 268, TRUE);
  MoveWindow(p.contents, listWidth + 28, 168, width - listWidth - 44, height - 268, TRUE);
  MoveWindow(p.status, 16, height - 88, width - 32, 34, TRUE);
  MoveWindow(p.save, width - 252, height - 46, 134, 32, TRUE);
  MoveWindow(p.cancelButton, width - 108, height - 46, 92, 32, TRUE);
}
void native_file(NativeReportPreview& p) {
  LRESULT index = SendMessageW(p.files, LB_GETCURSEL, 0, 0);
  if (index == LB_ERR || size_t(index) >= p.snapshot.files.size()) return;
  // The text control is a view of the frozen UTF-8 entry, not another read of
  // the original file. Export always uses the unmodified Snapshot bytes.
  const auto& entry = p.snapshot.files[size_t(index)];
  // A save is binary: show what it is, never its bytes.
  SetWindowTextW(p.contents, widen(entry.binary ? entry.note : entry.text).c_str());
}
void native_work(NativeReportPreview& p, std::wstring destination = {}) {
  p.busy = true; p.cancel = false; p.exporting = !destination.empty();
  EnableWindow(p.save, FALSE); EnableWindow(p.files, FALSE);
  SetWindowTextW(p.cancelButton, L"Cancel");
  SetWindowTextW(p.status, p.exporting ? L"Saving report…" : L"Preparing report…");
  p.worker = std::thread([&p, destination] {
    auto progress = [&p](size_t done, size_t total) {
      PostMessageW(p.window, native_progress, WPARAM(done), LPARAM(total));
      return !p.cancel.load();
    };
    try {
      if (p.exporting) p.exported = diagnostics::save_report_zip(p.snapshot, destination, progress);
      else {
        diagnostics::Request request;
        request.install_root = root_dir();
        request.include_save = p.includeSave;
        if (!p.cancel.load()) request.system_text = graphics_report(inspect_graphics());
        p.snapshot = diagnostics::prepare_report(request, progress);
      }
    } catch (...) {
      if (p.exporting) p.exported = {diagnostics::Status::Failed, "Report not saved. Preview kept; choose another file."};
      else { p.snapshot.status = diagnostics::Status::Failed; p.snapshot.message = "Report could not be prepared. Close and retry."; }
    }
    PostMessageW(p.window, native_done, 0, 0);
  });
}
LRESULT CALLBACK native_report_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
  auto* p = reinterpret_cast<NativeReportPreview*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    p = static_cast<NativeReportPreview*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
    p->window = window; SetWindowLongPtrW(window, GWLP_USERDATA, LONG_PTR(p));
  }
  if (!p) return DefWindowProcW(window, message, w, l);
  switch (message) {
    case WM_CREATE: {
      HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE));
      auto control = [&](const wchar_t* type, const wchar_t* text, DWORD style, int id) {
        return CreateWindowExW(!wcscmp(type, L"EDIT") || !wcscmp(type, L"LISTBOX") ? WS_EX_CLIENTEDGE : 0,
          type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, window, HMENU(INT_PTR(id)), instance, nullptr);
      };
      p->privacy = control(L"STATIC", widen(std::string(diagnostics::privacy_text)+"\n\n"+session_logging::hint(root_dir())).c_str(), 0, 0);
      p->files = control(L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP, 101);
      p->contents = control(L"EDIT", L"Select a file to review its contents.",
        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP, 102);
      SendMessageW(p->contents, EM_SETLIMITTEXT, 0x7ffffffe, 0);
      p->status = control(L"STATIC", L"Preparing report…", 0, 0);
      p->save = control(L"BUTTON", L"Save report…", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK);
      p->cancelButton = control(L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL);
      p->body = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
      p->mono = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH, L"Consolas");
      for (auto child : {p->privacy, p->files, p->status, p->save, p->cancelButton})
        SendMessageW(child, WM_SETFONT, WPARAM(p->body), TRUE);
      SendMessageW(p->contents, WM_SETFONT, WPARAM(p->mono), TRUE);
      EnableWindow(p->save, FALSE); native_layout(*p); return 0;
    }
    case WM_SIZE: native_layout(*p); return 0;
    case WM_GETMINMAXINFO: reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize = {760, 580}; return 0;
    case native_progress:
      if (p->busy) SetWindowTextW(p->status, ((p->exporting ? L"Saving report: " : L"Preparing report: ") +
        std::to_wstring(size_t(w)) + L" / " + std::to_wstring(size_t(l)) ).c_str());
      return 0;
    case native_done: {
      if (p->worker.joinable()) p->worker.join();
      p->busy = false;
      if (p->closeWhenDone) { DestroyWindow(window); return 0; }
      EnableWindow(p->files, TRUE); SetWindowTextW(p->cancelButton, L"Close");
      if (p->exporting) SetWindowTextW(p->status, widen(p->exported.message).c_str());
      else {
        SendMessageW(p->files, LB_RESETCONTENT, 0, 0);
        int extent = 0;
        for (const auto& file : p->snapshot.files) {
          auto label = widen(file.name) + L" (" + std::to_wstring(file.text.size()) + L" bytes)";
          SendMessageW(p->files, LB_ADDSTRING, 0, LPARAM(label.c_str()));
          if (int(label.size()) * 9 > extent) extent = int(label.size()) * 9;
        }
        SendMessageW(p->files, LB_SETHORIZONTALEXTENT, extent, 0);
        if (!p->snapshot.files.empty()) { SendMessageW(p->files, LB_SETCURSEL, 0, 0); native_file(*p); }
        SetWindowTextW(p->status, widen(p->snapshot.message + " Review every file before sharing. Nothing is uploaded.").c_str());
      }
      EnableWindow(p->save, !p->snapshot.files.empty() &&
        (p->snapshot.status == diagnostics::Status::Complete || p->snapshot.status == diagnostics::Status::Partial));
      return 0;
    }
    case WM_COMMAND:
      if (LOWORD(w) == 101 && HIWORD(w) == LBN_SELCHANGE) { native_file(*p); return 0; }
      if (LOWORD(w) == IDOK && !p->busy && !p->snapshot.files.empty()) {
        auto path = choose_zip(window); if (!path.empty()) native_work(*p, path); return 0;
      }
      if (LOWORD(w) != IDCANCEL) break;
      [[fallthrough]];
    case WM_CLOSE:
      if (p->busy) {
        p->cancel = true; p->closeWhenDone = true;
        EnableWindow(p->cancelButton, FALSE); SetWindowTextW(p->status, L"Cancelling…");
      } else DestroyWindow(window);
      return 0;
  }
  return DefWindowProcW(window, message, w, l);
}
}

int native_report(HWND owner) {
  NativeReportPreview preview;
  auto instance = GetModuleHandleW(nullptr);
  WNDCLASSW cls{}; cls.lpfnWndProc = native_report_proc; cls.hInstance = instance;
  cls.lpszClassName = L"BG3RTX.NativeReportPreview"; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1)); cls.hbrBackground = HBRUSH(COLOR_BTNFACE + 1);
  RegisterClassW(&cls);
  auto window = CreateWindowExW(WS_EX_DLGMODALFRAME, cls.lpszClassName, L"BG3RTX report preview",
    WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1060, 760, owner, nullptr, instance, &preview);
  if (!window) { MessageBoxW(owner, L"Report preview could not open. No files were collected or saved.", L"Report a problem", MB_OK | MB_ICONERROR); return 2; }
  bool restoreOwner = owner && IsWindowEnabled(owner);
  if (restoreOwner) EnableWindow(owner, FALSE);
  ShowWindow(window, SW_SHOW);
  // The latest save is opt-in: the default button is No.
  preview.includeSave = MessageBoxW(window, (widen(diagnostics::save_option_text) + L"?").c_str(), L"Report a problem",
    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
  native_work(preview);
  MSG message{};
  while (IsWindow(window)) {
    int result = GetMessageW(&message, nullptr, 0, 0);
    if (result <= 0) { if (result == 0) PostQuitMessage(int(message.wParam)); break; }
    if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
  }
  // A parent-loop quit must not leave a live window pointing at stack state.
  if (IsWindow(window)) {
    preview.cancel = true;
    if (preview.worker.joinable()) preview.worker.join();
    DestroyWindow(window);
  }
  if (restoreOwner) { EnableWindow(owner, TRUE); SetActiveWindow(owner); }
  return preview.exporting ? diagnostics::exit_code(preview.exported.status) : 3;
}
}


