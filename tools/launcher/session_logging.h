#pragma once
#include <string>
#include <string_view>
#include <windows.h>
namespace launcher::session_logging {
inline constexpr const char* suggestion = "For a more detailed report, turn on Detailed logging in Help & support, restart the game and reproduce the problem, then create the report.";
// The startup marker records the effective gate for the session, independently of later launcher edits.
inline int enabled(std::string_view text) {
  constexpr std::string_view marker = "[cfg] detailed logging: ";
  auto at=text.rfind(marker);
  if(at==std::string_view::npos)return -1;
  at+=marker.size();
  if(at>=text.size()||(text[at]!='0'&&text[at]!='1'))return -1;
  if(at+1<text.size()&&text[at+1]!='\r'&&text[at+1]!='\n')return -1;
  return text[at]-'0';
}
inline std::string hint(const std::wstring& root) {
  // Logs start afresh each session. Read only the bounded header, sharing the live writer.
  // Rotation/older logs without the marker are unknown: suggest without claiming it was off.
  HANDLE file=CreateFileW((root+L"\\bg3rtx.log").c_str(),GENERIC_READ,
    FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(file==INVALID_HANDLE_VALUE)return suggestion;
  BY_HANDLE_FILE_INFORMATION info{};char bytes[65536];DWORD count=0;
  bool ok=GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))&&
    ReadFile(file,bytes,sizeof bytes,&count,nullptr);
  CloseHandle(file);
  return ok&&enabled(std::string_view(bytes,count))==1?"":suggestion;
}
}
