#include "web_host.h"
#include "web_bridge.h"
#include "ui_state.h"
#include <wrl.h>
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

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;
namespace launcher::web {
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
  bool no_game{}, report{}, failed{}, ready{}, busy{};
  int result{};
  std::thread worker;
  std::atomic<bool> cancel{false};
  std::string reply, after_work;
  ~Host() { cancel=true; if(worker.joinable())worker.join(); if(controller)controller->Close(); }
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
void send(Host& h,const std::string& json) {if(h.view)h.view->PostWebMessageAsJson(widen(json).c_str());}
void refresh(Host& h) {
  if(h.busy||!h.bridge)return;
  auto session=session_token();
  if(session!=h.session){h.session=session;h.baseline=effective(h.state);h.bridge->set_restart_count(0);}
  h.bridge->set_mod_status(file_exists(exe_dir()+L"\\VkLayer_bg3rtx.dll")&&file_exists(exe_dir()+L"\\bg3rtx_core.dll")?"Mod files present":"Mod files missing");
  h.bridge->set_can_play(!h.no_game&&valid_game_dir(h.game)&&!game_running());
  send(h,h.bridge->state_json());
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
LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM w,LPARAM l) {
  auto* h=reinterpret_cast<Host*>(GetWindowLongPtrW(window,GWLP_USERDATA));
  if(message==WM_NCCREATE){h=static_cast<Host*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);h->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,LONG_PTR(h));}
  if(!h)return DefWindowProcW(window,message,w,l);
  switch(message){
    case WM_SETFOCUS:if(h->controller)h->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);return 0;
    case WM_SIZE:if(h->controller){RECT r{};GetClientRect(window,&r);h->controller->put_Bounds(r);}return 0;
    case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={736,620};return 0;
    case WM_TIMER:if(w==2)capture(*h);else if(w==3){h->result=2;DestroyWindow(window);}else refresh(*h);return 0;
    case work_done:if(h->worker.joinable())h->worker.join();h->busy=false;
      if(!h->after_work.empty()){h->reply=h->bridge->handle(h->after_work);h->after_work.clear();}
      send(*h,h->reply);return 0;
    case work_progress:send(*h,"{\"type\":\"progress\",\"done\":"+std::to_string(w)+",\"total\":"+std::to_string(l)+"}");return 0;
    case WM_CLOSE:
      if(h->busy){h->cancel=true;return 0;}
      if(h->bridge&&h->bridge->dirty()){
        int answer=MessageBoxW(window,L"Save your changes before closing?\nYes: review Save   No: Discard   Cancel: keep editing",L"Unsaved changes",MB_YESNOCANCEL|MB_ICONQUESTION);
        if(answer==IDCANCEL)return 0;
        if(answer==IDYES){send(*h,h->bridge->handle("{\"type\":\"save\"}"));return 0;}
      }DestroyWindow(window);return 0;
    case WM_DESTROY:KillTimer(window,1);KillTimer(window,2);KillTimer(window,3);PostQuitMessage(0);return 0;
  }return DefWindowProcW(window,message,w,l);
}
}

int run(HINSTANCE inst,const std::wstring& game,bool report,bool no_game,const std::wstring& screenshot,int width,const std::wstring& shot_view) {
  LPWSTR version{};
  if(FAILED(GetAvailableCoreWebView2BrowserVersionString(nullptr,&version)))return screenshot.empty()?-1:2;
  CoTaskMemFree(version);
  std::wstring ui=exe_dir()+L"\\ui";
  if(!file_exists(ui+L"\\index.html"))return screenshot.empty()?-1:2;
  HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  if(FAILED(com))return -1;
  int rc=0;
  {
    Host h;h.no_game=no_game||report||!screenshot.empty();h.report=report;h.result=report?3:0;h.shot=screenshot;h.shot_view=shot_view;
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
        if(h.no_game){error="Review mode cannot write settings.";return false;}
        auto forced=forced_keys(candidate);forced.insert(forced.end(),required.begin(),required.end());
        auto text=st::render_user_ini(candidate.user_text,candidate.preset,candidate.values,forced);
        auto after=candidate.base;st::apply_layer(after,st::parse_ini(text));
        bool pending=game_running()&&ui_state::restart(h.baseline,after);std::wstring err;
        if(!write_file_atomic(root_dir()+L"\\bg3rtx_ui.ini",ui_state::serialize(4,pending,h.session,h.baseline),&err)||!write_file_atomic(user_ini_path(),text,&err)){error=narrow(err);return false;}
        candidate.user_text=text;candidate.user=st::parse_ini(text);candidate.loaded=candidate.values;
        h.bridge->set_restart_count(pending?ui_state::restart_count(h.baseline,after):0);return true;
      };
      hooks.play=[&h](std::string& error){
        if(h.no_game||game_running()||!valid_game_dir(h.game)){error="Choose the game folder containing bin\\bg3.exe (Vulkan), or wait for the running game to close.";return false;}
        DWORD pid{};std::wstring err;bool ok=launch(make_plan(h.game,L"",{},false,effective_fg(h.state)),&pid,&err);error=narrow(err);return ok;
      };
      hooks.prepareReport=[&h](const std::string& issue,bool logs,bool settings,bool system){
        diagnostics::Request r;r.install_root=root_dir();r.issue=issue;r.include_logs=logs;r.include_settings=settings;r.include_system=system;
        if(system)r.system_text=graphics_report(inspect_graphics());
        return diagnostics::prepare_report(r,[&h](size_t done,size_t total){PostMessageW(h.window,work_progress,done,total);return !h.cancel.load();});
      };
      hooks.exportReport=[&h](const diagnostics::Snapshot& snapshot,std::string& error){
        if(h.export_path.empty()){error="Cancelled. Preview retained.";return false;}
        auto result=diagnostics::save_report_zip(snapshot,h.export_path,[&h](size_t done,size_t total){PostMessageW(h.window,work_progress,done,total);return !h.cancel.load();});if(h.report)h.result=diagnostics::exit_code(result.status);error=result.message;return result.status==diagnostics::Status::Complete||result.status==diagnostics::Status::Partial;
      };
      hooks.browseGame=[&h](){
        if(h.no_game)return narrow(h.game);
        ComPtr<IFileOpenDialog> dialog;
        if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return narrow(h.game);
        dialog->SetOptions(FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR);
        dialog->SetTitle(L"Choose the game folder containing bin\\bg3.exe (Vulkan)");
        if(SUCCEEDED(dialog->Show(h.window))){ComPtr<IShellItem> item;if(SUCCEEDED(dialog->GetResult(&item))){PWSTR path{};if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){auto dir=game_dir_from_path(path);if(valid_game_dir(dir)){h.game=dir;save_game_dir(dir);}else h.bridge->set_status("Choose the folder containing bin\\bg3.exe (Vulkan).");CoTaskMemFree(path);}}}
        h.bridge->set_can_play(valid_game_dir(h.game)&&!game_running());return narrow(h.game);
      };
      hooks.recheck=[](){return inspect_graphics();};
      h.bridge=std::make_unique<Bridge>(h.state,std::move(hooks));
      h.bridge->set_game_path(narrow(h.game));
      h.bridge->set_compatibility(inspect_graphics());h.bridge->set_restart_count(pending?ui_state::restart_count(h.baseline,effective(h.state)):0);
      h.bridge->set_status(valid_game_dir(h.game)?"Saved settings apply on next launch. Select the adapter used by the game.":"Choose the game folder containing bin\\bg3.exe (Vulkan). Settings remain available.");
    }
    WNDCLASSW cls{};cls.lpfnWndProc=window_proc;cls.hInstance=inst;cls.lpszClassName=L"BG3RTX.WebView";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=LoadIconW(inst,MAKEINTRESOURCEW(1));RegisterClassW(&cls);
    RECT r{0,0,width>0?width:1180,760};AdjustWindowRect(&r,WS_OVERLAPPEDWINDOW,FALSE);
    CreateWindowW(cls.lpszClassName,L"BG3RTX",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,nullptr,nullptr,inst,&h);
    PWSTR local{};std::wstring profile;
    if(!screenshot.empty())profile=exe_dir()+L"\\webview-review-profile";
    else if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local))){profile=std::wstring(local)+L"\\BG3RTX\\WebView2";CoTaskMemFree(local);}
    if(profile.empty()){DestroyWindow(h.window);h.failed=true;}
    else {
      auto options=Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
      options->put_AdditionalBrowserArguments(L"--disable-background-networking --disable-component-update --disable-sync --no-first-run");
      HRESULT started=CreateCoreWebView2EnvironmentWithOptions(nullptr,profile.c_str(),options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([&h,ui](HRESULT result,ICoreWebView2Environment* env)->HRESULT {
          if(FAILED(result)||!env){fail(h,"Environment",result);return S_OK;}
          h.environment=env;
          HRESULT started=env->CreateCoreWebView2Controller(h.window,Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([&h,ui](HRESULT result,ICoreWebView2Controller* controller)->HRESULT {
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
                if(h.export_path.empty()){h.bridge->set_status("Cancelled. Preview retained.");send(h,h.bridge->state_json());return S_OK;}
              }
              // Only bounded allowlisted collection runs on a worker; mutations stay serialized.
              if(Bridge::is_collection_intent(text)||Bridge::is_export_intent(text)){
                h.busy=true;h.cancel=false;h.worker=std::thread([&h,text]{try{h.reply=h.bridge->handle(text);}catch(...){h.bridge->set_status("Report operation failed. Preview and settings are retained.");h.reply=h.bridge->state_json();}PostMessageW(h.window,work_done,0,0);});}
              else send(h,h.bridge->handle(text));return S_OK;
            }).Get(),&token);
            h.view->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>([&h](ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs* args)->HRESULT {
              BOOL ok{};args->get_IsSuccess(&ok);if(!ok){COREWEBVIEW2_WEB_ERROR_STATUS error{};args->get_WebErrorStatus(&error);fail(h,"Navigation",HRESULT(error));return S_OK;}h.ready=true;KillTimer(h.window,3);
              if(!h.shot.empty())SetTimer(h.window,2,1600,nullptr);
              else {refresh(h);if(h.report)send(h,h.bridge->handle("{\"type\":\"report\"}"));SetTimer(h.window,1,2000,nullptr);}return S_OK;
            }).Get(),&token);
            RECT bounds{};GetClientRect(h.window,&bounds);controller->put_Bounds(bounds);controller->put_IsVisible(TRUE);
            std::wstring url=L"https://app.bg3rtx.local/index.html";
            if(!h.shot.empty())url+=L"?sample=1&view="+h.shot_view;
            h.view->Navigate(url.c_str());return S_OK;
          }).Get());if(FAILED(started))fail(h);return S_OK;
        }).Get());
      if(FAILED(started))fail(h);
    }
    if(!h.failed){ShowWindow(h.window,SW_SHOWNOACTIVATE);SetTimer(h.window,3,60000,nullptr);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    rc=h.failed?(screenshot.empty()?-1:2):h.result;
  }
  if(rc==-1){MSG quit{};PeekMessageW(&quit,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE);}
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
  bool busy{}, exporting{}, closeWhenDone{};
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
  MoveWindow(p.privacy, 16, 12, width - 32, 94, TRUE);
  MoveWindow(p.files, 16, 114, listWidth, height - 214, TRUE);
  MoveWindow(p.contents, listWidth + 28, 114, width - listWidth - 44, height - 214, TRUE);
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
  SetWindowTextW(p.contents, widen(entry.text).c_str());
}
void native_work(NativeReportPreview& p, std::wstring destination = {}) {
  p.busy = true; p.cancel = false; p.exporting = !destination.empty();
  EnableWindow(p.save, FALSE); EnableWindow(p.files, FALSE);
  SetWindowTextW(p.cancelButton, L"Cancel");
  SetWindowTextW(p.status, p.exporting ? L"Writing the reviewed snapshot..." : L"Preparing bounded, sanitized report files...");
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
        if (!p.cancel.load()) request.system_text = graphics_report(inspect_graphics());
        p.snapshot = diagnostics::prepare_report(request, progress);
      }
    } catch (...) {
      if (p.exporting) p.exported = {diagnostics::Status::Failed, "Report export failed. Preview retained; choose another ZIP path to retry."};
      else { p.snapshot.status = diagnostics::Status::Failed; p.snapshot.message = "Report collection failed. Close this preview and retry."; }
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
      p->privacy = control(L"STATIC", widen(diagnostics::privacy_text).c_str(), 0, 0);
      p->files = control(L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP, 101);
      p->contents = control(L"EDIT", L"Select a file to review its exact sanitized contents.",
        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP, 102);
      SendMessageW(p->contents, EM_SETLIMITTEXT, 0x7ffffffe, 0);
      p->status = control(L"STATIC", L"Preparing report...", 0, 0);
      p->save = control(L"BUTTON", L"Create ZIP...", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK);
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
      if (p->busy) SetWindowTextW(p->status, ((p->exporting ? L"Writing reviewed files: " : L"Preparing report: ") +
        std::to_wstring(size_t(w)) + L" / " + std::to_wstring(size_t(l)) + L". Cancel stops this operation.").c_str());
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
        EnableWindow(p->cancelButton, FALSE); SetWindowTextW(p->status, L"Cancelling safely...");
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
  auto window = CreateWindowExW(WS_EX_DLGMODALFRAME, cls.lpszClassName, L"BG3RTX - Report preview (local only)",
    WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1060, 760, owner, nullptr, instance, &preview);
  if (!window) { MessageBoxW(owner, L"The native report preview could not be opened. Nothing was collected or written.", L"Report a problem", MB_OK | MB_ICONERROR); return 2; }
  bool restoreOwner = owner && IsWindowEnabled(owner);
  if (restoreOwner) EnableWindow(owner, FALSE);
  ShowWindow(window, SW_SHOW); native_work(preview);
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


