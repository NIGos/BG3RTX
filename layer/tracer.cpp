// Frame tracer: on request (Ctrl+Shift+F10, or a file named trace.request in
// the repository root) records one complete frame as a JSON pass graph under
// captures/<stamp>/frame.json. Shader modules are dumped once, by hash, to
// captures/shaders/<hash>.spv as the game creates them.
#include "host.h"

#include <unordered_set>

namespace bg3 {

static std::atomic<bool> g_recording{false};
static uint64_t g_start = 0;
static std::mutex g_mu;
struct SubmitRec {
  uint64_t frame;
  std::string json;
};
static std::vector<SubmitRec> g_submits;
static std::mutex g_ref_mu;
static std::unordered_set<VkImage> g_refs;
static bool g_key_was_down = false;

bool trace_recording() { return g_recording.load(std::memory_order_relaxed); }

static std::string hex64(uint64_t v) {
  char b[24];
  snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
  return b;
}

// Caller holds tracker().mu (shared or exclusive).
static std::string ref_locked(VkImage image) {
  auto it = tracker().images.find(image);
  if (it == tracker().images.end()) return "?";
  {
    std::lock_guard<std::mutex> lock(g_ref_mu);
    g_refs.insert(image);
  }
  return "i" + std::to_string(it->second.id);
}

std::string image_ref(VkImage image) {
  std::shared_lock<std::shared_mutex> lock(tracker().mu);
  return ref_locked(image);
}

std::string view_ref(VkImageView view) {
  std::shared_lock<std::shared_mutex> lock(tracker().mu);
  auto it = tracker().views.find(view);
  return it == tracker().views.end() ? "?" : ref_locked(it->second.image);
}

void trace_event(CmdState* s, std::string json) { s->events.push_back(std::move(json)); }

void trace_flush_segment(CmdState* s) {
  if (!s->seg_count) return;
  s->events.push_back(s->seg_json + ",\"n\":" + std::to_string(s->seg_count) + "}");
  s->seg_count = 0;
  s->seg_key.clear();
}

static const char* desc_tag(VkDescriptorType t) {
  switch (t) {
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: return "rw";
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: return "in";
    default: return "r";
  }
}

void trace_note_draw(CmdState* s, int bp, const char* kind, const char* extra) {
  const uint32_t kMaxPerSet = 32;
  std::string res;
  {
    std::shared_lock<std::shared_mutex> lock(tracker().mu);
    auto add = [&](const std::string& slot, const DescEntry& e) {
      if (!e.view) return;
      auto v = tracker().views.find(e.view);
      if (v == tracker().views.end()) return;
      res += (res.empty() ? "\"" : ",\"") + slot + "\":\"" + desc_tag(e.type) + ":" + ref_locked(v->second.image) + "\"";
    };
    for (uint32_t k = 0; k < 8; ++k) {
      VkDescriptorSet set = s->sets[bp][k];
      if (!set) continue;
      auto it = tracker().sets.find(set);
      if (it == tracker().sets.end()) continue;
      uint32_t n = 0, skipped = 0;
      for (const auto& e : it->second) {
        if (!e.view) continue;
        if (n++ >= kMaxPerSet) { ++skipped; continue; }
        add("s" + std::to_string(k) + "b" + std::to_string(e.binding) +
                (e.element ? "[" + std::to_string(e.element) + "]" : std::string()), e);
      }
      if (skipped)
        res += (res.empty() ? "\"" : ",\"") + std::string("s") + std::to_string(k) + "_truncated\":\"" +
               std::to_string(skipped) + "\"";
    }
    for (const auto& e : s->push[bp])
      add("push_b" + std::to_string(e.binding) + (e.element ? "[" + std::to_string(e.element) + "]" : std::string()), e);
  }

  std::string key = std::string(kind) + "|" + std::to_string(s->pipe_id[bp]) + "|" + res + "|" + (extra ? extra : "");
  if (key == s->seg_key) {
    ++s->seg_count;
    return;
  }
  trace_flush_segment(s);
  s->seg_key = std::move(key);
  s->seg_count = 1;
  std::string j = std::string("{\"op\":\"") + kind + "\",\"pipe\":" + std::to_string(s->pipe_id[bp]);
  if (bp == 0) j += ",\"vs\":\"" + hex64(s->vs) + "\",\"fs\":\"" + hex64(s->fs) + "\"";
  else j += ",\"cs\":\"" + hex64(s->cs) + "\"";
  j += ",\"res\":{" + res + "}";
  if (extra) j += std::string(",") + extra;
  s->seg_json = std::move(j);
}

void trace_on_submit(const std::vector<VkCommandBuffer>& cmds) {
  std::string j = "{\"cmds\":[";
  bool first = true;
  for (VkCommandBuffer c : cmds) {
    CmdState* s = cmd_state(c);
    char head[96];
    snprintf(head, sizeof head, "{\"cmd\":\"%p\",\"traced\":%s,\"events\":[", static_cast<void*>(c),
             s && s->tracing ? "true" : "false");
    j += (first ? "" : ",") + std::string(head);
    first = false;
    if (s && s->tracing) {
      trace_flush_segment(s);
      for (size_t i = 0; i < s->events.size(); ++i) j += (i ? ",\n" : "\n") + s->events[i];
    }
    j += "]}";
  }
  j += "]}";
  std::lock_guard<std::mutex> lock(g_mu);
  g_submits.push_back({frame_index(), std::move(j)});
}

void shader_dump(uint64_t hash, const void* code, size_t size) {
  static std::mutex mu;
  static std::unordered_set<uint64_t> done;
  if (!cfg_int("dump_shaders", 1)) return;
  std::lock_guard<std::mutex> lock(mu);
  if (!done.insert(hash).second) return;
  static const std::string dir = [] {
    std::string d = root_dir() + "\\captures\\shaders";
    make_dirs(d);
    return d;
  }();
  std::string path = dir + "\\" + hex64(hash) + ".spv";
  if (!file_mtime(path)) write_file(path, code, size);
}

static void write_trace(uint64_t frame, const VkPresentInfoKHR* info) {
  std::string dir = root_dir() + "\\captures\\" + now_stamp();
  make_dirs(dir);

  std::string j = "{\n\"frame\":" + std::to_string(frame) + ",\n\"submits\":[\n";
  size_t n = 0, cmds = 0;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    for (auto& s : g_submits)
      if (s.frame == frame) {
        j += (n++ ? ",\n" : "") + s.json;
        cmds += 1;
      }
    g_submits.clear();
  }
  j += "\n],\n\"present\":{\"image_index\":" +
       std::to_string(info && info->pImageIndices ? info->pImageIndices[0] : 0) + "},\n\"images\":{\n";

  std::unordered_set<VkImage> refs;
  {
    std::lock_guard<std::mutex> lock(g_ref_mu);
    refs.swap(g_refs);
  }
  bool first = true;
  {
    std::shared_lock<std::shared_mutex> lock(tracker().mu);
    for (VkImage img : refs) {
      auto it = tracker().images.find(img);
      if (it == tracker().images.end()) continue;
      const auto& ci = it->second.ci;
      char b[320];
      snprintf(b, sizeof b,
               "%s\"i%u\":{\"fmt\":%d,\"w\":%u,\"h\":%u,\"d\":%u,\"mips\":%u,\"layers\":%u,\"samples\":%d,"
               "\"usage\":%u,\"type\":%d,\"swapchain\":%s}",
               first ? "" : ",\n", it->second.id, ci.format, ci.extent.width, ci.extent.height, ci.extent.depth,
               ci.mipLevels, ci.arrayLayers, ci.samples, ci.usage, ci.imageType,
               it->second.swapchain ? "true" : "false");
      j += b;
      first = false;
    }
  }
  j += "\n}\n}\n";
  write_file(dir + "\\frame.json", j.data(), j.size());
  log("[trace] frame %llu written: %zu submits, %zu images -> %s", static_cast<unsigned long long>(frame), n,
      refs.size(), dir.c_str());
}

void trace_on_present(VkQueue, const VkPresentInfoKHR* info) {
  uint64_t frame = frame_index();
  if (!g_recording) {
    bool down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
                (GetAsyncKeyState(VK_F10) & 0x8000);
    bool key = down && !g_key_was_down;
    g_key_was_down = down;
    std::string req = root_dir() + "\\trace.request";
    bool file = file_mtime(req) != 0;
    if (file) DeleteFileA(req.c_str());
    if (key || file) {
      {
        std::lock_guard<std::mutex> lock(g_mu);
        g_submits.clear();
      }
      {
        std::lock_guard<std::mutex> lock(g_ref_mu);
        g_refs.clear();
      }
      g_start = frame;
      g_recording = true;
      log("[trace] recording from present %llu (%s)", static_cast<unsigned long long>(frame), key ? "hotkey" : "file");
    }
    return;
  }
  // Submissions are tagged with the present count at submit time; the frame
  // tagged start+1 is the first whose command buffers were all begun while recording.
  if (frame >= g_start + 2) {
    g_recording = false;
    write_trace(g_start + 1, info);
  }
}

}  // namespace bg3
