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
  State state;load_state(state);check(!state.settings_readable);
  check(!save_state(state,&error));fail_open=false;
  DeleteFileW(path);
  std::printf("launcher launch: %d checks, %d failures\n",checks,failed);
  return failed?1:0;
}
