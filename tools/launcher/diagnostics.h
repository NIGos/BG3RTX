#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace launcher::diagnostics {
inline constexpr const char* privacy_text =
  "Saves selected logs, settings and graphics details as a ZIP on your PC. "
  "Personal paths are partly removed; review every file before sharing. Nothing is uploaded. "
  "Screenshots and crash dumps are excluded; your latest save is added only if you tick it.";
// Opt-in label for the latest save (web dialog checkbox, native prompt, Diagnostics.cmd prompt).
inline constexpr const char* save_option_text =
  "Include my latest save (helps reproduce the problem; contains your character and progress)";
// Saves are binary and stored unredacted: one bounded slot per report (.lsv + its .webp thumbnail).
inline constexpr uint64_t default_max_save_bytes = 200ull * 1024 * 1024;
enum class Status { Complete, Partial, Cancelled, Failed };
struct Entry {
  std::string name, text, note; // Exact sanitized UTF-8 bytes to preview and export.
  bool binary = false; // Save file bytes: exported unchanged, never shown or redacted.
};
struct Snapshot {
  Status status = Status::Failed;
  std::vector<Entry> files;
  std::string message;
  uint64_t bytes = 0; // Sanitized payload; ZIP also adds bounded headers.
};
struct Request {
  std::filesystem::path install_root;
  std::string issue; // Optional, retained by caller on export failure, sanitized in snapshot.
  std::string system_text; // graphics_report(inspect_graphics()); empty recorded as unavailable.
  uint64_t max_file_bytes = 4 * 1024 * 1024, max_total_bytes = 16 * 1024 * 1024;
  size_t max_files = 32;
  bool include_logs = true, include_settings = true, include_system = true;
  bool include_save = false; // Opt-in, unchecked by default.
  std::filesystem::path player_profiles; // Empty: default_player_profiles(). Tests pass a fixture.
  uint64_t max_save_bytes = default_max_save_bytes;
};
// The newest Story save by last-write time:
// <PlayerProfiles>/<profile>/Savegames/Story/<folder>/*.lsv (+ that folder's .webp thumbnail).
// Bounded, nonrecursive listing; links are skipped; no file is opened.
struct LatestSave {
  std::filesystem::path folder, lsv, webp; // webp empty when the folder has none.
  uint64_t bytes = 0;                      // lsv + webp
  uint64_t write_time = 0;                 // lsv last write, FILETIME ticks
};
bool find_latest_save(const std::filesystem::path& player_profiles, LatestSave& out);
// %LOCALAPPDATA%/Larian Studios/Baldur's Gate 3/PlayerProfiles (empty if LOCALAPPDATA is unset).
std::filesystem::path default_player_profiles();
// Returning false cancels. Called for each file and bounded read/write chunk.
// Run on a worker thread; callbacks must marshal progress to the UI thread.
using Progress = std::function<bool(size_t done, size_t total)>;
std::string redact_text(std::string text, const std::filesystem::path& install_root = {});
// prepare_report: read-only inspection, no output/temp files, no source mutations.
// Fixed allowlist; never reads launcher INI, game folders, binaries or links, and saves only
// with include_save (the newest one, opened read-only with writers denied).
// Preview Snapshot.files (including manifest) verbatim BEFORE asking to save.
Snapshot prepare_report(const Request&, Progress = {});
struct ExportResult { Status status = Status::Failed; std::string message; };
// save_report_zip: create-new only, chosen absolute .zip path, parent must exist.
// Exports frozen preview bytes, never reopens sources. No upload/shell actions.
// Failure offers Retry / Choose another folder; caller retains Snapshot and issue.
ExportResult save_report_zip(const Snapshot&, const std::filesystem::path&, Progress = {});
// A2 --report dispatch: prepare_report -> preview/privacy -> chosen path ->
// save_report_zip; exit mapping: Complete=0, Partial=1, Failed=2, Cancelled=3.
int exit_code(Status);
} // namespace launcher::diagnostics
