// Disposable local fixtures only. No launcher/game/installer execution.
#include "../tools/launcher/diagnostics.h"
#include "../tools/launcher/compatibility.h"
#include "../tools/launcher/launcher.h"
#include <cstring>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace d = launcher::diagnostics;
static int checks = 0;
static void check(bool b, const char* message) { ++checks; if (!b) throw std::runtime_error(message); }
static void write(const fs::path& p, const std::string& text) { std::ofstream f(p, std::ios::binary); f << text; if (!f) throw std::runtime_error("fixture write failed"); }
static const d::Entry* entry(const d::Snapshot& s, const char* name) { for (const auto& e : s.files) if (e.name == name) return &e; return nullptr; }
static std::string all(const d::Snapshot& s) { std::string t; for (auto& e : s.files) t += e.text; return t; }
int wmain(int argc, wchar_t** argv) {
  SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
  try {
    if (argc != 2) throw std::runtime_error("pass fresh disposable fixture root");
    fs::path root = fs::absolute(argv[1]);
    if (!fs::create_directory(root)) throw std::runtime_error("fixture root must not exist");
    fs::create_directory(root / L"hair_grooms");
    check(!launcher::local_grooms_available(root.wstring()), "no local grooms in empty folder");
    write(root / L"hair_grooms" / L"bad.hgrm", "corrupt");
    check(!launcher::local_grooms_available(root.wstring()), "corrupt groom is unavailable");
    std::string groom(40+16+2*17+4, '\0');
    auto word=[&](size_t offset,uint32_t value){std::memcpy(groom.data()+offset,&value,4);};
    std::memcpy(groom.data(),"HGRM",4);word(4,1);word(16,1);word(24,3);word(28,3);word(32,1);
    float keep=1;std::memcpy(groom.data()+40,&keep,4);word(44,2);word(48,1);
    write(root / L"hair_grooms" / L"valid.hgrm", groom);
    check(launcher::local_grooms_available(root.wstring()), "valid indexed local groom available");
    write(root / L"hair_grooms" / L"valid.hgrm", groom.substr(0,40));
    check(!launcher::local_grooms_available(root.wstring()), "missing LOD headers unavailable");
    fs::create_directory(root / L"sl_logs");
    write(root / L"bg3rtx.log", "[cfg] knobs: preset=low\n[device] GPU\nC:\\Users\\OtherPerson\\secret.txt\npassword=secret-value\n");
    write(root / L"bg3rtx.prev.log", "old session");
    write(root / L"bg3rtx.ini", "rt=0\n");
    write(root / L"bg3rtx_user.ini", "preset=low\n");
    write(root / L"VERSION.txt", "fixture-version\n");
    write(root / L"bg3rtx_launcher.ini", "NEVER_COLLECT_ME");
    write(root / L"sl_logs" / L"sl.log", "Bearer abcdefghi\n");
    write(root / L"sl_logs" / L"private.bin", "NEVER_COLLECT_ME");
    // Old timestamp: stale data must be explicitly labelled.
    HANDLE old = CreateFileW((root / L"bg3rtx.prev.log").c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    FILETIME oldtime{}; SYSTEMTIME year{}; year.wYear = 2020; year.wMonth = 1; year.wDay = 1; SystemTimeToFileTime(&year, &oldtime);
    SetFileTime(old, nullptr, nullptr, &oldtime); CloseHandle(old);
    const auto source_time = fs::last_write_time(root / L"bg3rtx.log");
    d::Request r; r.install_root = root; r.issue = "Issue token=not-public"; r.system_text = "GPU fixture";
    auto s = d::prepare_report(r);
    check(s.status == d::Status::Partial, "stale report is partial");
    check(entry(s, "CONTENTS.txt") && entry(s, "system/system.txt"), "manifest and hardware exist");
    check(all(s).find("older than 24 hours") != std::string::npos, "stale labelled");
    check(all(s).find("OtherPerson") == std::string::npos && all(s).find("secret-value") == std::string::npos, "paths/credentials redacted");
    check(all(s).find("NEVER_COLLECT_ME") == std::string::npos, "allowlist excludes launcher ini and binary");
    check(all(s).find("not-public") == std::string::npos, "issue sanitized");
    check(entry(s, "settings/effective-log.txt")->text.find("NOT currently saved") != std::string::npos, "effective settings provenance");
    check(fs::last_write_time(root / L"bg3rtx.log") == source_time, "source last-write unchanged");
    check(all(s).find("CRC32") != std::string::npos, "defaults identity");
    FILETIME current{}; GetSystemTimeAsFileTime(&current);
    old = CreateFileW((root / L"bg3rtx.prev.log").c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    SetFileTime(old, nullptr, nullptr, &current); CloseHandle(old);
    auto complete = d::prepare_report(r);
    check(complete.status == d::Status::Complete, "all allowlisted inputs available gives complete report");
    write(root / L"bg3rtx.log", "[cfg] knobs: earlier-config\n[cfg] keys: old-key\n[cfg] knobs: latest-config\n[cfg] keys: new-key\n");
    auto latest = d::prepare_report(r);
    check(entry(latest, "settings/effective-log.txt")->text.find("earlier-config") == std::string::npos &&
      entry(latest, "settings/effective-log.txt")->text.find("latest-config") != std::string::npos, "only latest logged configuration extracted");
    auto t = d::redact_text("C:/Users/Alice/foo C:\\\\Users\\\\Bob\\\\bar password='hello world' api_key=abc Bearer def https://name:pass@site ghp_1234567890123456\ncommandLine=private data\n");
    for (const char* secret : {"Alice", "Bob", "hello world", "=abc", "Bearer def", "name:pass", "ghp_123", "private data"}) check(t.find(secret) == t.npos, "redaction fixture");
    auto cancelled = d::prepare_report(r, [](size_t, size_t) { return false; });
    check(cancelled.status == d::Status::Cancelled && cancelled.files.empty(), "prepare cancellation");
    HANDLE locked = CreateFileW((root / L"bg3rtx.log").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    auto partial = d::prepare_report(r); CloseHandle(locked);
    check(partial.status == d::Status::Partial && !entry(partial, "logs/bg3rtx.log"), "locked log partial");
    check(all(partial).find("unreadable") != std::string::npos, "per-file errors recorded");
    write(root / L"sl_logs" / L"bad.log", std::string("\xff\xfe", 2));
    check(all(d::prepare_report(r)).find("invalid UTF-8") != std::string::npos, "unsupported text encoding recorded");
    r.max_file_bytes = 8;
    auto bounded = d::prepare_report(r);
    check(all(bounded).find("oversized") != std::string::npos && !entry(bounded, "logs/bg3rtx.log"), "oversize skips whole input");
    r.max_file_bytes = 4 * 1024 * 1024;
    r.max_files = 8;
    auto count_bound = d::prepare_report(r);
    check(count_bound.files.size() <= 8, "file count bounded including manifest");
    r.max_files = 32;
    r.max_total_bytes = 65536;
    write(root / L"sl_logs" / L"big.log", std::string(40000, 'x'));
    auto total_bound = d::prepare_report(r);
    check(total_bound.bytes <= r.max_total_bytes && all(total_bound).find("size/file limit") != std::string::npos, "total bound");
    r.max_total_bytes = 16 * 1024 * 1024;
    fs::create_directory(root / L"empty");
    d::Request empty; empty.install_root = root / L"empty";
    auto missing = d::prepare_report(empty);
    check(missing.status == d::Status::Partial && entry(missing, "system/system.txt"), "missing logs still useful report");
    empty.include_logs = false; empty.include_settings = false;
    auto hardware_only = d::prepare_report(empty);
    check(hardware_only.files.size() == 2 && !entry(hardware_only, "settings/effective-log.txt"), "content selection respected");
    empty.include_system = false;
    check(d::prepare_report(empty).status == d::Status::Failed, "no empty-success report");
    check(d::prepare_report(d::Request{root / L".."}).status == d::Status::Failed, "path traversal refused");
    // Directory junction to our own fixture only, created without symlink privilege.
    // The PowerShell harness creates this for the reparse test after the first run.
    if (fs::exists(root.parent_path() / L"fixture-link")) {
      empty.install_root = root.parent_path() / L"fixture-link";
      check(d::prepare_report(empty).status == d::Status::Failed, "ancestor junction refused");
      auto linked = d::save_report_zip(s, empty.install_root / L"escape.zip");
      check(linked.status == d::Status::Failed, "linked destination refused");
    }
    auto destination = root / L"report.zip";
    fs::create_directory(root / L"expected");
    for (const auto& e : s.files) {
      auto expected = root / L"expected" / fs::path(e.name);
      fs::create_directories(expected.parent_path()); write(expected, e.text);
    }
    auto exported = d::save_report_zip(s, destination);
    check(exported.status == s.status && fs::file_size(destination) > s.bytes, "zip saved");
    check(d::save_report_zip(s, destination).status == d::Status::Failed, "existing output never overwritten");
    check(d::save_report_zip(s, root / L"missing" / L"report.zip").status == d::Status::Failed, "destination failure retains snapshot");
    check(d::save_report_zip(s, root / L".." / L"escape.zip").status == d::Status::Failed, "destination traversal refused");
    check(d::save_report_zip(s, root / L"cancel-before.zip", [](size_t, size_t) { return false; }).status == d::Status::Cancelled, "early export cancellation");
    int calls = 0;
    auto cancel_mid = d::save_report_zip(s, root / L"cancel-mid.zip", [&](size_t, size_t) { return ++calls < 5; });
    check(cancel_mid.status == d::Status::Cancelled && !fs::exists(root / L"cancel-mid.zip"), "cancel deletes owned output only");
    check(fs::exists(destination) && !s.files.empty(), "retry retains preview and prior report");
    auto bad = s; bad.files[0].name = "../escape";
    check(d::save_report_zip(bad, root / L"bad.zip").status == d::Status::Failed, "zip entry traversal refused");
    launcher::GraphicsInspection g;
    check(launcher::assess_compatibility(g).caps == 0, "unknown never supported");
    launcher::GraphicsAdapter a;
    a.gpu = {0x10de, 0x2900, "GeForce RTX 5090", true, true, false};
    a.driver_version = "621.01"; a.dedicated_bytes = 32ull << 30;
    a.budget_known = true; a.budget_bytes = 30ull << 30; a.usage_bytes = 31ull << 30;
    g.adapters.push_back(a);
    auto c = launcher::assess_compatibility(g);
    check(c.hardware == launcher::CheckState::Supported && c.driver == launcher::CheckState::Supported, "RTX test driver recognized");
    check(c.memory_text.find("31.7") != c.memory_text.npos && c.memory_text.find("0 MiB") != c.memory_text.npos, "memory caution and saturated headroom");
    for (int gb : {8, 12, 16, 24, 32}) {
      g.adapters[0].dedicated_bytes = uint64_t(gb) << 30;
      c = launcher::assess_compatibility(g);
      check(c.memory_text.find("Risk estimate") != c.memory_text.npos, "VRAM tier is estimate");
    }
    g.adapters[0].driver_version = "500.00";
    c = launcher::assess_compatibility(g);
    check(c.driver == launcher::CheckState::Warning && c.driver_text.find("Untested") != c.driver_text.npos && c.driver_text.find("minimum") != c.driver_text.npos, "old driver not false minimum");
    g.adapters[0].gpu.vendor = 0x1002;
    check(launcher::assess_compatibility(g).hardware == launcher::CheckState::Warning, "non-NVIDIA RT distinguished");
    g.adapters[0].gpu.ray_query = false;
    check(launcher::assess_compatibility(g).hardware == launcher::CheckState::Unsupported, "no RT distinguished");
    g.adapters.push_back(a);
    check(launcher::assess_compatibility(g).caps == 0 && launcher::assess_compatibility(g, 1).caps != 0, "multiple adapters require choice");
    check(d::exit_code(d::Status::Partial) == 1 && d::exit_code(d::Status::Cancelled) == 3, "CLI exit mapping");
    // Own-process, read-only Vulkan inspection only. No device or shaders.
    SetEnvironmentVariableW(L"VK_LOADER_LAYERS_DISABLE", L"fixture-preserved");
    auto observed = launcher::inspect_graphics();
    wchar_t restored[64]{}; GetEnvironmentVariableW(L"VK_LOADER_LAYERS_DISABLE", restored, 64);
    check(std::wstring(restored) == L"fixture-preserved", "probe restores loader environment");
    SetEnvironmentVariableW(L"VK_LOADER_LAYERS_DISABLE", nullptr);
    check(!observed.adapters.empty() || !observed.error.empty(), "probe returns adapters or actionable error");
    std::cout << checks << " checks passed; ZIP fixture: " << destination.u8string().size() << " path bytes\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << "FAILED: " << e.what() << '\n'; return 1; }
}
