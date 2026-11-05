// CPU-only launch plan and injected Win32 I/O failures. Never starts a process.
#include "launcher.h"
#include <cstdio>
static bool fail_open=false, fail_read=false, fail_flush=false;
static HANDLE WINAPI test_create(const wchar_t* name,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES sa,DWORD creation,DWORD flags,HANDLE pattern) {
  if(fail_open){SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}
  return CreateFileW(name,access,share,sa,creation,flags,pattern);
}
static BOOL WINAPI test_read(HANDLE h,LPVOID out,DWORD size,LPDWORD count,LPOVERLAPPED overlap) {
  if(fail_read){SetLastError(ERROR_READ_FAULT);return FALSE;}
  return ReadFile(h,out,size,count,overlap);
}
static BOOL WINAPI test_flush(HANDLE h) {return fail_flush?FALSE:FlushFileBuffers(h);}
#define CreateFileW test_create
#define ReadFile test_read
#define FlushFileBuffers test_flush
#include "launch.cpp"
#undef CreateFileW
#undef ReadFile
#undef FlushFileBuffers
int main() {
  int failed=0,checks=0;
  auto check=[&](bool ok){++checks;if(!ok){++failed;std::printf("FAIL check %d\n",checks);}};
  using namespace launcher;
  auto p=make_plan(L"C:\\Game",L"",{L"a b",L"quote\"",L"ends \\"},false,2);
  check(p.exe==L"C:\\Game\\bin\\bg3.exe" && p.cwd==L"C:\\Game\\bin");
  check(command_line(p).find(L"\"a b\"")!=std::wstring::npos);
  const wchar_t inherited[]=L"=C:=C:\\work\0Path=old\0bg3rtx_assetdb=bad\0BG3RTX_SE_DIR=bad\0BG3RTX_SAVE_LOG=bad\0VK_INSTANCE_LAYERS=bad\0";
  std::wstring env;
  check(!compose_environment(p,nullptr,env));
  check(compose_environment(p,inherited,env));
  check(env.find(L"=C:=C:\\work") == 0);
  check(env.find(L"Path=old")!=std::wstring::npos);
  for(const auto& key:p.env_unset)check(env.find(key+L"=")==std::wstring::npos);
  check(env.find(L"bg3rtx_assetdb=")==std::wstring::npos);
  check(env.find(L"VK_INSTANCE_LAYERS=VK_LAYER_BG3RTX_host")!=std::wstring::npos);
  check(env[env.size()-1]==0&&env[env.size()-2]==0);
  LaunchPlan empty;check(compose_environment(empty,L"",env)&&env.size()==2);
  auto clean=make_plan(L"",L"C:\\test.exe",{},true,0);
  check(clean.env_set.size()>p.env_set.size()-2);
  wchar_t dir[MAX_PATH], path[MAX_PATH];GetTempPathW(MAX_PATH,dir);GetTempFileNameW(dir,L"bg3",0,path);
  std::wstring error;std::string text;
  check(write_file_atomic(path,"previous",&error));
  fail_read=true;check(!read_file(path,text)&&text.empty());fail_read=false;
  fail_flush=true;check(!write_file_atomic(path,"replacement",&error));fail_flush=false;
  check(read_file(path,text)&&text=="previous");
  fail_open=true;check(!save_game_dir(L"C:\\Game",&error)&&!error.empty());
  State bindings;bindings.shortcuts[0]="Alt+R";bindings.shortcuts[1]="None";
  auto rendered=bg3::settings::render_user_ini("unmanaged=retained\nhotkey_dlss5=F1\n",bg3::settings::kHigh,bg3::settings::preset_values(bg3::settings::kHigh),forced_keys(bindings));
  auto keys=bg3::settings::parse_ini(rendered);
  check(bg3::settings::lookup(keys,"hotkey_rt_toggle")=="Alt+R");
  check(bg3::settings::lookup(keys,"hotkey_dlss5")=="None");
  check(rendered.find("unmanaged=retained")!=std::string::npos);
  State state;load_state(state);check(!state.settings_readable);
  check(!save_state(state,&error));fail_open=false;
  DeleteFileW(path);
  // Game upscaler guard: the reset file of 2026-11-05 (no upscaler entries) and a DLSS file.
  {
    const std::string head="<save>\n    <region id=\"Config\">\n        <node id=\"root\">\n            <children>\n";
    const std::string tail="            </children>\n        </node>\n    </region>\n</save>\n";
    auto entry=[](const char* k,const char* v){return std::string("                <node id=\"ConfigEntry\">\n                    <attribute id=\"MapKey\" type=\"FixedString\" value=\"")+k+"\"/>\n                    <attribute id=\"Type\" type=\"int32\" value=\"0\"/>\n                    <attribute id=\"Value\" type=\"int32\" value=\""+v+"\"/>\n                </node>\n";};
    const std::string reset=head+entry("ScreenWidth","3840")+tail;
    std::vector<std::string> ch;
    std::string fixed=guard_upscaler_text(reset,&ch);
    check(ch.size()==2&&ch[0]=="ResolutionUpscaleType added=3"&&ch[1]=="DLSS2Mode added=2");
    check(fixed.find(entry("ResolutionUpscaleType","3"))!=std::string::npos);
    check(fixed.find(entry("DLSS2Mode","2"))!=std::string::npos);
    check(fixed.find(entry("ScreenWidth","3840"))!=std::string::npos);
    ch.clear();check(guard_upscaler_text(fixed,&ch)==fixed&&ch.empty());           // idempotent
    const std::string fsr=head+entry("DLSS2Mode","4")+entry("ResolutionUpscaleType","1")+tail;
    ch.clear();std::string f2=guard_upscaler_text(fsr,&ch);
    check(ch.size()==1&&ch[0]=="ResolutionUpscaleType 1->3");
    check(f2.find(entry("DLSS2Mode","4"))!=std::string::npos);                     // player's DLSS mode kept
    check(f2.find(entry("ResolutionUpscaleType","3"))!=std::string::npos);
    std::string crlf;for(char c:reset){if(c=='\n')crlf+='\r';crlf+=c;}
    ch.clear();std::string f3=guard_upscaler_text(crlf,&ch);
    size_t lf=0,bare=0;for(size_t i=0;i<f3.size();++i)if(f3[i]=='\n'){++lf;if(!i||f3[i-1]!='\r')++bare;}
    check(ch.size()==2&&lf>0&&bare==0);                                             // CRLF file stays CRLF
    ch.clear();check(guard_upscaler_text("not xml",&ch)=="not xml"&&ch.empty());   // unknown file left alone
    // End to end on a temporary LOCALAPPDATA: off when switched off or with rr=0, on by default, then the reset file is repaired with a backup.
    wchar_t tmp[MAX_PATH];GetTempPathW(MAX_PATH,tmp);
    std::wstring local=std::wstring(tmp)+L"bg3rtx_guard_test_"+std::to_wstring(GetCurrentProcessId());
    std::wstring larian=local+L"\\Larian Studios",gdir=larian+L"\\Baldur's Gate 3",lsx=gdir+L"\\graphicSettings.lsx";
    CreateDirectoryW(local.c_str(),nullptr);CreateDirectoryW(larian.c_str(),nullptr);CreateDirectoryW(gdir.c_str(),nullptr);
    wchar_t old_local[MAX_PATH]={};DWORD had_local=GetEnvironmentVariableW(L"LOCALAPPDATA",old_local,MAX_PATH);
    SetEnvironmentVariableW(L"LOCALAPPDATA",local.c_str());
    check(write_file_atomic(lsx,reset,&error));
    State g;g.base=bg3::settings::parse_ini("rr=1\ngame_dlss_guard=0\n");
    check(guard_game_upscaler(g).empty());                                         // switched off
    g.base=bg3::settings::parse_ini("rr=0\ngame_dlss_guard=1\n");
    check(guard_game_upscaler(g).empty());                                         // RR off: nothing to protect
    g.base=bg3::settings::parse_ini("rr=1\n");                                     // key absent: default on
    check(read_file(lsx,text)&&text==reset);
    check(guard_game_upscaler(g).find(L"set back to DLSS")!=std::wstring::npos);
    check(read_file(lsx,text)&&text==fixed);
    check(read_file(lsx+L".bg3rtx-bak",text)&&text==reset);
    check(guard_game_upscaler(g).empty());                                         // already DLSS
    DeleteFileW(lsx.c_str());DeleteFileW((lsx+L".bg3rtx-bak").c_str());
    RemoveDirectoryW(gdir.c_str());RemoveDirectoryW(larian.c_str());RemoveDirectoryW(local.c_str());
    SetEnvironmentVariableW(L"LOCALAPPDATA",had_local?old_local:nullptr);
  }
  std::printf("launcher launch: %d checks, %d failures\n",checks,failed);
  return failed?1:0;
}
