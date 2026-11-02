// common/log_rotate.h: bg3rtx.log size cap (previous session and full files -> bg3rtx.prev.log).
#include "../common/log_rotate.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <windows.h>
using namespace bg3::logrot;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static std::string slurp(const std::string& p){FILE* f=std::fopen(p.c_str(),"rb");if(!f)return "<none>";std::string s;char b[4096];size_t n;while((n=std::fread(b,1,sizeof b,f))>0)s.append(b,n);std::fclose(f);return s;}
// Binary, so sizes are exact (the layer opens in text mode: CRLF adds 1 byte per line to the cap).
static FILE* openb(const char* p){return std::fopen(p,"wb");}
static bool exists(const std::string& p){return GetFileAttributesA(p.c_str())!=INVALID_FILE_ATTRIBUTES;}
int main(){
 CHECK(prev_path("C:\\x\\bg3rtx.log")=="C:\\x\\bg3rtx.prev.log");
 CHECK(prev_path("C:\\x\\bg3rtx.run1.log")=="C:\\x\\bg3rtx.run1.prev.log");
 CHECK(prev_path("C:\\x.y\\bg3rtx")=="C:\\x.y\\bg3rtx.prev");
 CHECK(!due(0,1000,100));            // an empty file takes any line
 CHECK(!due(50,50,100) && due(51,50,100));
 CHECK(kLogCapBytes==64ull<<20);

 char tmp[MAX_PATH];GetTempPathA(MAX_PATH,tmp);
 const std::string dir=std::string(tmp)+"bg3rtx_log_rotate_test_"+std::to_string(GetCurrentProcessId());
 CreateDirectoryA(dir.c_str(),nullptr);
 const std::string log=dir+"\\bg3rtx.log",prev=dir+"\\bg3rtx.prev.log";
 // A previous session's file moves to prev at the first line; an older prev is replaced.
 {FILE* f=std::fopen(log.c_str(),"wb");std::fputs("old session\n",f);std::fclose(f);}
 {FILE* f=std::fopen(prev.c_str(),"wb");std::fputs("older session\n",f);std::fclose(f);}
 {
  Writer w;w.path=log;w.cap=100;w.open=openb;
  CHECK(w.write("0123456789012345678901234567890123456789\n"));   // 41 bytes
  CHECK(slurp(prev)=="old session\n");
  CHECK(w.write("0123456789012345678901234567890123456789\n"));   // 82
  CHECK(w.rotations==0);
  CHECK(w.write("abcdefghijabcdefghijabcdefghij\n"));             // 82+31 > 100: rotate
  CHECK(w.rotations==1);
  std::string cur=slurp(log);
  CHECK(cur.find("[log] reached")==0 && cur.find(prev)!=std::string::npos);
  CHECK(cur.substr(cur.size()-31)=="abcdefghijabcdefghijabcdefghij\n");
  CHECK(slurp(prev).size()==82);                                   // the full part, not the old session
  // Many lines: never more than two files, each at most cap + one line + the note.
  for(int i=0;i<200;++i) CHECK(w.write("line line line line\n"));
  CHECK(w.rotations>=30);
  CHECK(slurp(log).size()<=100+20+MAX_PATH+64 && slurp(prev).size()<=100+20+MAX_PATH+64);   // cap + a line + the note
  std::fclose(w.f);
 }
 // A line longer than the cap in an empty file is still written whole.
 {
  Writer w;w.path=log;w.cap=10;w.open=openb;
  std::string big(50,'x');big+="\n";
  CHECK(w.write(big.c_str()) && slurp(log)==big);
  std::fclose(w.f);
 }
 // No file at all: shift fails quietly, the open creates it.
 DeleteFileA(log.c_str());DeleteFileA(prev.c_str());
 CHECK(!shift(log));
 {Writer w;w.path=log;CHECK(w.write("first\n"));CHECK(!exists(prev));std::fclose(w.f);}
 DeleteFileA(log.c_str());DeleteFileA(prev.c_str());RemoveDirectoryA(dir.c_str());
 std::printf("log_rotate_test: %u checks passed\n",checks);
 return 0;
}
