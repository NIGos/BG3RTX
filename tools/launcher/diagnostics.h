#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace launcher::diagnostics {
inline constexpr const char* privacy_text =
  "Creates a ZIP on your PC with BG3RTX logs, graphics settings, version and system graphics details. "
  "Logs may contain file paths. We remove common personal path details, but you should review the "
  "files before sharing. Nothing is uploaded. Saves, screenshots and crash dumps are excluded. "
  "This collector does not accept attachments.";
enum class Status { Complete, Partial, Cancelled, Failed };
struct Entry {
  std::string name, text, note; // Exact sanitized UTF-8 bytes to preview and export.
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
};
// Returning false cancels. Called for each file and bounded read/write chunk.
// Run on a worker thread; callbacks must marshal progress to the UI thread.
using Progress = std::function<bool(size_t done, size_t total)>;
std::string redact_text(std::string text, const std::filesystem::path& install_root = {});
// prepare_report: read-only inspection, no output/temp files, no source mutations.
// Fixed allowlist; never reads launcher INI, game folders, saves, binaries or links.
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
