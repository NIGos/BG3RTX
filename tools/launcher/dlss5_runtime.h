// Launcher-only runtime discovery/staging. Never linked to the Vulkan host and
// never copies a runtime into the release payload. No DLL is loaded to inspect it.
#pragma once
#include "launcher.h"
#include "dlss5_runtime_policy.h"
#include <wintrust.h>
#include <softpub.h>
#include <bcrypt.h>
#include <filesystem>
#include <array>
#include <cstdio>
#include <shobjidl.h>

namespace launcher::dlss5 {
namespace fs = std::filesystem;
struct File {
  HANDLE handle = INVALID_HANDLE_VALUE;
  explicit File(const fs::path& path) : handle(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) {}
  File(const File&) = delete;
  ~File() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};
inline std::string sha256(HANDLE file) {
  LARGE_INTEGER start{}; if (!SetFilePointerEx(file,start,nullptr,FILE_BEGIN)) return {};
  BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hash{};
  if (BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return {};
  DWORD size{},read{}; bool ok=BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof size,&read,0)>=0;
  std::vector<unsigned char> object(size),block(1024*1024);std::array<unsigned char,32> digest{};
  if(ok)ok=BCryptCreateHash(algorithm,&hash,object.data(),size,nullptr,0,0)>=0;
  while(ok){if(!ReadFile(file,block.data(),DWORD(block.size()),&read,nullptr)){ok=false;break;}if(!read)break;ok=BCryptHashData(hash,block.data(),read,0)>=0;}
  if(ok)ok=BCryptFinishHash(hash,digest.data(),DWORD(digest.size()),0)>=0;
  if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
  if(!ok)return {};std::string result;for(auto byte:digest){char hex[3];std::snprintf(hex,sizeof hex,"%02x",byte);result+=hex;}return result;
}
struct Inspection {
  fs::path path;
  CandidateStatus status;
  std::string hash,version;
  LONG trust = TRUST_E_NOSIGNATURE;
};
inline Inspection inspect(const fs::path& path) {
  Inspection out;out.path=path;File input(path);if(input.handle==INVALID_HANDLE_VALUE)return out;
  out.hash=sha256(input.handle);if(out.hash.empty())return out;
  IMAGE_DOS_HEADER dos{};DWORD read{};LARGE_INTEGER offset{};
  if(!SetFilePointerEx(input.handle,offset,nullptr,FILE_BEGIN)||!ReadFile(input.handle,&dos,sizeof dos,&read,nullptr)||read!=sizeof dos||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0)return out;
  offset.QuadPart=dos.e_lfanew;DWORD signature{};IMAGE_FILE_HEADER pe{};
  if(!SetFilePointerEx(input.handle,offset,nullptr,FILE_BEGIN)||!ReadFile(input.handle,&signature,sizeof signature,&read,nullptr)||read!=sizeof signature||signature!=IMAGE_NT_SIGNATURE||!ReadFile(input.handle,&pe,sizeof pe,&read,nullptr)||read!=sizeof pe||pe.Machine!=IMAGE_FILE_MACHINE_AMD64)return out;
  DWORD ignored{},bytes=GetFileVersionInfoSizeW(path.c_str(),&ignored);std::vector<unsigned char> version(bytes);VS_FIXEDFILEINFO* info{};UINT count{};
  if(!bytes||!GetFileVersionInfoW(path.c_str(),0,bytes,version.data())||!VerQueryValueW(version.data(),L"\\",reinterpret_cast<void**>(&info),&count)||count<sizeof(*info))return out;
  out.version=std::to_string(HIWORD(info->dwFileVersionMS))+"."+std::to_string(LOWORD(info->dwFileVersionMS))+"."+std::to_string(HIWORD(info->dwFileVersionLS))+"."+std::to_string(LOWORD(info->dwFileVersionLS));
  out.status.compatible=HIWORD(info->dwFileVersionMS)==310&&(LOWORD(info->dwFileVersionMS)==8||LOWORD(info->dwFileVersionMS)==9);
  WINTRUST_FILE_INFO file{sizeof file};file.pcwszFilePath=path.c_str();file.hFile=input.handle;
  WINTRUST_DATA trust{sizeof trust};trust.dwUIChoice=WTD_UI_NONE;trust.fdwRevocationChecks=WTD_REVOKE_NONE;trust.dwUnionChoice=WTD_CHOICE_FILE;trust.pFile=&file;trust.dwStateAction=WTD_STATEACTION_VERIFY;trust.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL;
  GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;out.trust=WinVerifyTrust(nullptr,&action,&trust);
  if(out.trust==ERROR_SUCCESS){auto* provider=WTHelperProvDataFromStateData(trust.hWVTStateData);auto* signer=provider?WTHelperGetProvSignerFromChain(provider,0,FALSE,0):nullptr;
    if(signer&&signer->csCertChain){wchar_t name[256]{};CertGetNameStringW(signer->pasCertChain[0].pCert,CERT_NAME_ATTR_TYPE,0,const_cast<char*>(szOID_ORGANIZATION_NAME),name,256);out.status.signed_nvidia=std::wstring(name)==L"NVIDIA Corporation";}}
  trust.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&action,&trust);return out;
}
inline void log(const std::string& line) {
  auto path=root_dir()+L"\\bg3rtx.dlss5-runtime.log";HANDLE file=CreateFileW(path.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
  std::string text=line+"\r\n";DWORD written{};if(file!=INVALID_HANDLE_VALUE){WriteFile(file,text.data(),DWORD(text.size()),&written,nullptr);FlushFileBuffers(file);CloseHandle(file);}OutputDebugStringW((widen(line)+L"\n").c_str());
}
inline const char* trust_status(const Inspection& file) {
  if(!file.status.compatible)return "MissingOrIncompatible";
  if(file.trust==ERROR_SUCCESS)return file.status.signed_nvidia?"ValidNVIDIA":"ValidOtherPublisher";
  if(file.trust==TRUST_E_BAD_DIGEST)return "HashMismatch";
  if(file.trust==TRUST_E_NOSIGNATURE)return "Unsigned";
  return "Unverified";
}
inline std::string discovery_status(const State& state,const std::wstring& game) {
  std::vector<Inspection> files;
  for(const auto& path:candidate_paths(game,widen(state.dlss5_runtime_path)))files.push_back(inspect(path));
  std::vector<CandidateStatus> states;for(const auto& file:files)states.push_back(file.status);
  auto chosen=select(states);
  if(!chosen){
    bool exists=false;for(const auto& file:files){std::error_code error;if(fs::is_regular_file(file.path,error))exists=true;}
    return exists?"Runtime rejected: no compatible DLSS 5 runtime. Choose nvngx_dlssnr.dll version 310.8 or 310.9 (x64).":"No DLSS 5 runtime found. Browse for nvngx_dlssnr.dll version 310.8 or 310.9 (x64).";
  }
  const auto& file=files[*chosen];
  if(file.status.signed_nvidia)return "NVIDIA-signed runtime found: "+file.version+". Prepared before Play.";
  return "Only custom/unsigned runtimes found: "+file.version+" ("+trust_status(file)+"). NVIDIA core may reject it (0xbad0000b). Prepared before Play.";
}
inline bool enabled(const State& state) {
  auto index=bg3::settings::knob_index("dlss5_nr");if(index>=0&&size_t(index)<state.values.size()&&state.values[index]>=0)return state.values[index]>0;
  bg3::settings::Map effective;bg3::settings::apply_layer(effective,state.base);bg3::settings::apply_layer(effective,state.user);return bg3::settings::lookup(effective,"dlss5_nr")=="1";
}
inline bool stage(const State& state,const std::wstring& game,std::wstring& note,std::wstring& error) {
  note.clear();error.clear();if(!enabled(state))return true;
  if(game_running()){error=L"Close the game before preparing the DLSS 5 runtime.";return false;}
  const fs::path destination=fs::path(exe_dir())/L"sl/nvngx_dlssnr.dll";
  const auto paths=candidate_paths(game,widen(state.dlss5_runtime_path));
  std::vector<Inspection> candidates;std::vector<CandidateStatus> status;
  for(auto& path:paths){candidates.push_back(inspect(path));const auto& file=candidates.back();status.push_back(file.status);
    char code[16];std::snprintf(code,sizeof code,"0x%08x",unsigned(file.trust));log("DLSS5 candidate path="+narrow(path)+" version="+file.version+" sha256="+file.hash+" trust="+trust_status(file)+"("+code+") NVIDIA_signed="+(file.status.signed_nvidia?"1":"0")+" compatible="+(file.status.compatible?"1":"0"));}
  auto selected=select(status);if(!selected){error=L"DLSS 5 needs nvngx_dlssnr.dll version 310.8 or 310.9 (x64). Choose your runtime file.";return false;}
  auto& chosen=candidates[*selected];File locked(chosen.path);
  if(locked.handle==INVALID_HANDLE_VALUE||sha256(locked.handle)!=chosen.hash){error=L"The DLSS 5 runtime changed while being checked. Try again.";return false;}
  auto existing=inspect(destination);
  if(existing.hash==chosen.hash){note=L"DLSS 5 runtime ready (unchanged): "+widen(chosen.version)+(chosen.status.signed_nvidia?L", NVIDIA-signed.":L", custom/unsigned; NVIDIA may reject it (0xbad0000b).");log("DLSS5 staging unchanged hash="+chosen.hash);return true;}
  std::error_code ec;fs::create_directories(destination.parent_path(),ec);if(ec){error=L"Cannot prepare the DLSS 5 runtime folder.";return false;}
  fs::path temporary=destination;temporary+=L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
  HANDLE target=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(target==INVALID_HANDLE_VALUE){error=L"Cannot stage the DLSS 5 runtime.";return false;}
  LARGE_INTEGER start{};bool ok=SetFilePointerEx(locked.handle,start,nullptr,FILE_BEGIN)!=0;std::vector<unsigned char> block(1024*1024);DWORD read{},written{};
  while(ok){if(!ReadFile(locked.handle,block.data(),DWORD(block.size()),&read,nullptr)){ok=false;break;}if(!read)break;ok=WriteFile(target,block.data(),read,&written,nullptr)&&written==read;}
  ok=FlushFileBuffers(target)&&ok;CloseHandle(target);auto staged=inspect(temporary);ok=ok&&staged.hash==chosen.hash;
  if(game_running())ok=false;
  if(ok)ok=MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
  if(!ok){DeleteFileW(temporary.c_str());error=L"Cannot replace the staged DLSS 5 runtime. Close the game and retry.";return false;}
  note=L"DLSS 5 runtime prepared: "+widen(chosen.version)+(chosen.status.signed_nvidia?L", NVIDIA-signed.":L", custom/unsigned; NVIDIA may reject it (0xbad0000b).");
  log("DLSS5 staged byte-identical source="+narrow(chosen.path.wstring())+" destination="+narrow(destination.wstring())+" sha256="+chosen.hash);return true;
}
inline std::string browse(HWND owner,const std::string& current) {
  IFileOpenDialog* dialog{};if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return current;
  const COMDLG_FILTERSPEC filter[]={{L"DLSS 5 runtime (nvngx_dlssnr.dll)",L"nvngx_dlssnr.dll"}};dialog->SetFileTypes(1,filter);dialog->SetOptions(FOS_FORCEFILESYSTEM|FOS_FILEMUSTEXIST|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR);dialog->SetTitle(L"Choose your DLSS 5 runtime");
  std::string selected=current;if(SUCCEEDED(dialog->Show(owner))){IShellItem* item{};if(SUCCEEDED(dialog->GetResult(&item))){PWSTR path{};if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){selected=narrow(path);CoTaskMemFree(path);}item->Release();}}dialog->Release();return selected;
}
}
