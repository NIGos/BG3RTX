// Frame tracer: on request (Ctrl+Shift+F10, or a file named trace.request in
// the repository root) records one complete frame as a JSON pass graph under
// captures/<stamp>/frame.json. Shader modules are dumped once, by hash, to
// captures/shaders/<hash>.spv as the game creates them.
#include "host.h"
#include "../common/native_vt_surface.h"
#include "../common/trace_terrain_table.h"
#include "omm_sampler.h"

#include <thread>
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
static std::atomic<uint32_t> g_passes{0}, g_draws{0};
// auto trigger state
static uint32_t g_streak = 0, g_auto_done = 0;
static uint64_t g_auto_next_allowed = 0;

void count_pass() { g_passes.fetch_add(1, std::memory_order_relaxed); }
static std::atomic<uint64_t> g_diag[DIAG_COUNT];
static std::atomic<uint64_t> g_watch[DIAG_COUNT];
void diag(DiagCounter c, uint64_t n) {
  g_diag[c].fetch_add(n, std::memory_order_relaxed);
  g_watch[c].fetch_add(n, std::memory_order_relaxed);
  if (c == DIAG_DRAW) g_draws.fetch_add(uint32_t(n), std::memory_order_relaxed);
}

// Logs activity every watchdog_s seconds from its own thread, so a stalled
// render thread still leaves a record of what the process was doing.
void watchdog_start() {
  static std::once_flag once;
  std::call_once(once, [] {
    std::thread([] {
      for (;;) {
        int s = cfg_int("watchdog_s", 5);
        Sleep(DWORD((s > 0 ? s : 5) * 1000));
        if (cfg_int("watchdog_s", 5) <= 0) continue;
        uint64_t d[DIAG_COUNT];
        for (int i = 0; i < DIAG_COUNT; ++i) d[i] = g_watch[i].exchange(0);
        log("[watch] presents %llu (displayed %llu) | pipelines %llu (%.0f ms in driver) | shader modules %llu | descriptor writes %llu | "
            "submits %llu | draws %llu",
            d[DIAG_PRESENT], d[DIAG_DISPLAYED], d[DIAG_PIPELINES], d[DIAG_PIPELINE_US] / 1000.0, d[DIAG_SHADER_MODULES],
            d[DIAG_DESC_WRITES], d[DIAG_SUBMIT], d[DIAG_DRAW]);
      }
    }).detach();
  });
}

bool trace_recording() { return g_recording.load(std::memory_order_relaxed); }
uint64_t trace_start_frame() { return g_start; }

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

void trace_note_draw(VkCommandBuffer cmd, CmdState* s, int bp, const char* kind, const char* extra) {
  const std::string terrain = bp == 0 ? terrain_capture_note(cmd,s) : std::string();
  const bool terrainTable=trace_terrain::requested(bp==0,trace_recording(),true,s->vs,s->fs) &&
      file_mtime(root_dir()+"\\trace_terrain_packet.request")!=0;
  omm::OrdinaryBindingFacts tableFacts{};
  const bool tableFrozen=terrainTable && omm::ordinary_frozen_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,2,32,0,&tableFacts);
  const uint32_t kMaxPerSet = 32;
  std::string res, sampledViews, terrainViews;
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
      for (const auto& [key, e] : it->second) {
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
    // The missing M3a observable: exact view interpretation of the four audited
    // native VT bindings. Fixed four entries, independent of generic res limits.
    // This records CPU draw bindings; it does not attest GPU descriptor/content epochs.
    if(bp==0 && native_vt::recipe(s->fs))for(uint32_t binding=3;binding<=6;++binding){
      const DescEntry* entry=nullptr;
      if(s->push_set[0]==1){for(const auto& e:s->push[0])if(e.binding==binding && e.element==0)entry=&e;}
      else if(auto set=tracker().sets.find(s->sets[0][1]);set!=tracker().sets.end()){
        auto e=set->second.find(desc_key(binding,0));if(e!=set->second.end())entry=&e->second;
      }
      std::string value="{\"status\":\"unbound\"}";
      if(entry && entry->view){
        auto view=tracker().views.find(entry->view);
        if(view==tracker().views.end())value="{\"status\":\"unavailable_view\"}";
        else {
          const auto image=tracker().images.find(view->second.image);const auto& v=view->second;
          value=trace_view::json(v.format,v.range,v.creation,image==tracker().images.end()?0:image->second.id,
             image==tracker().images.end()?nullptr:&image->second.ci,entry->type,entry->layout);
          ref_locked(v.image);
        }
      }
      sampledViews+=(sampledViews.empty()?"":",")+std::string("\"s1b")+std::to_string(binding)+"\":"+value;
    }

    if(terrainTable){
      const bool declaredKnown=tableFacts.allocated && tableFacts.layout_complete && !tableFacts.push &&
          tableFacts.count!=0 && tableFacts.type==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
      const auto range=trace_terrain::range(declaredKnown,tableFacts.count);
      uint32_t missing=0,unavailable=0;std::string entries;
      auto set=tracker().sets.find(s->sets[0][2]);
      for(uint32_t element=0;element<range.emitted;++element){
        const DescEntry* entry=nullptr;
        if(set!=tracker().sets.end()){
          auto found=set->second.find(desc_key(32,element));if(found!=set->second.end())entry=&found->second;
        }
        std::string value;
        if(!entry || !entry->view){value="{\"status\":\"unwritten_or_null\"}";++missing;}
        else if(auto view=tracker().views.find(entry->view);view==tracker().views.end()){
          value="{\"status\":\"unavailable_view\"}";++unavailable;
        }else{
          const auto& v=view->second;const auto image=tracker().images.find(v.image);
          value=trace_view::json(v.format,v.range,v.creation,image==tracker().images.end()?0:image->second.id,
              image==tracker().images.end()?nullptr:&image->second.ci,entry->type,entry->layout);
          ref_locked(v.image);
        }
        entries+=(entries.empty()?"":",")+std::string("\"")+std::to_string(element)+"\":"+value;
      }
      const char* status=!declaredKnown?"unknown_declared_range":!tableFrozen?"unsupported_or_mutable_layout":
          range.truncated?"truncated":missing||unavailable?"incomplete_entries":"recorded";
      terrainViews="{\"scope\":\"CPU_recording_metadata_not_execution_or_DDS_identity\",\"status\":\""+std::string(status)+
        "\",\"set\":2,\"binding\":32,\"descriptor_set_id\":"+std::to_string(tableFacts.set_id)+
        ",\"layout_id\":"+std::to_string(tableFacts.layout_id)+",\"write_version\":"+std::to_string(tableFacts.version)+
        ",\"binding_flags\":"+std::to_string(tableFacts.flags)+",\"declared_count\":"+(declaredKnown?std::to_string(range.declared):"null")+
        ",\"cap\":"+std::to_string(trace_terrain::table_cap)+",\"emitted\":"+std::to_string(range.emitted)+
        ",\"truncated\":"+std::to_string(range.truncated)+",\"unwritten_or_null\":"+std::to_string(missing)+
        ",\"unavailable_views\":"+std::to_string(unavailable)+",\"entries\":{"+entries+"}}";
    }

  }

  std::string key = std::string(kind) + "|" + std::to_string(s->pipe_id[bp]) + "|" + res + "|" + sampledViews + "|" + (extra ? extra : "") + terrain;
  if(!terrainViews.empty())key+="|terrain_texture_table|"+terrainViews;
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
  if(!sampledViews.empty())j+=",\"sampled_views_scope\":\"Base_AlphaTest_VT_DEF_recording\",\"sampled_views\":{"+sampledViews+"}";
  if (extra) j += std::string(",") + extra;
  if (!terrain.empty()) j += ",\"terrain_packet\":" + terrain;
  if(!terrainViews.empty())j+=",\"terrain_texture_table\":"+terrainViews;
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
  {
    std::lock_guard<std::mutex> lock(g_mu);
    g_submits.push_back({frame_index(), std::move(j)});
  }
  dumps_on_submit(cmds, frame_index());
}

void shader_dump(uint64_t hash, const void* code, size_t size) {
  static std::mutex mu;
  static std::unordered_set<uint64_t> done;
  if (!cfg_int("dump_shaders", 0)) return;   // 1 in the repository's bg3rtx.ini, 0 in release
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

static std::string write_trace(uint64_t frame, const VkPresentInfoKHR* info) {
  std::string dir = root_dir() + "\\captures\\" + capture_stamp();
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
  return dir;
}

// Auto trigger (auto_trace=1): once the frame has had at least
// auto_min_passes render passes for auto_stable_frames presents in a row (the
// game is past loading screens), take auto_traces traces auto_interval
// presents apart, then write captures/auto.done.
// auto_traces=0 takes no trace: auto.done is written as soon as the scene is
// stable ("[auto] scene ready"), for test-queue.ps1 sessions, which then take
// their traces on demand through trace.request.
static bool auto_trigger(uint64_t frame, uint32_t passes, uint32_t draws) {
  if (frame % 300 == 0) {
    uint64_t d[DIAG_COUNT];
    for (int i = 0; i < DIAG_COUNT; ++i) d[i] = g_diag[i].exchange(0);
    log("[auto] present %llu: %u passes, %u draws in the last frame | last 300 presents: submit %llu begin_cmd %llu "
        "render_pass %llu bind %llu draw %llu dispatch %llu",
        (unsigned long long)frame, passes, draws, d[DIAG_SUBMIT], d[DIAG_BEGIN_CMD], d[DIAG_BEGIN_PASS],
        d[DIAG_BIND_PIPE], d[DIAG_DRAW], d[DIAG_DISPATCH]);
  }
  if (!cfg_int("auto_trace", 0)) return false;
  int want = cfg_int("auto_traces", 1);
  static bool ready_marked = false;
  if (int(g_auto_done) >= want && (want > 0 || ready_marked)) return false;
  // A present with no passes at all (a driver-inserted one) neither counts nor resets.
  // Draws tell gameplay (~3000/frame at 3840x1600) from the 3D main menu (~1300).
  // Stability is measured in time as well as frames: with the display asleep
  // Windows throttles presentation to a few frames per second.
  static ULONGLONG streak_start = 0;
  if (passes) {
    // auto_max_draws picks a lighter view, e.g. the docks the -Move pan reaches
    // after a minute or two (about 780 draws, outdoors, docs/TRACK_D_RT.md).
    bool good = passes >= uint32_t(cfg_int("auto_min_passes", 40)) && draws >= uint32_t(cfg_int("auto_min_draws", 2000)) &&
                draws <= uint32_t(cfg_int("auto_max_draws", 1000000));
    // auto_after_load: only after a loading screen that follows a 3D scene,
    // i.e. the 3D main menu has given way to the loaded save. Needed for scenes
    // that draw about as much as the menu (auto-run.ps1 -Save).
    static bool seen_scene = false, loaded = false;
    // Both transitions are logged once: auto-run.ps1 -AcceptModWarning reads them to tell
    // "3D main menu up, no load started" (the missing-mods dialog) from a load in progress.
    if (passes >= 40) {
      if (!seen_scene) log("[auto] 3D scene first seen at present %llu (%u passes, %u draws)", (unsigned long long)frame, passes, draws);
      seen_scene = true;
    } else if (seen_scene && passes < 10) {
      if (!loaded) log("[auto] loading screen after the 3D scene at present %llu", (unsigned long long)frame);
      loaded = true;
    }
    if (cfg_int("auto_after_load", 0) && !loaded) good = false;
    if (good && !g_streak) streak_start = GetTickCount64();
    g_streak = good ? g_streak + 1 : 0;
  }
  bool stable = g_streak >= uint32_t(cfg_int("auto_stable_frames", 600)) ||
                (g_streak >= 10 && GetTickCount64() - streak_start >= ULONGLONG(cfg_int("auto_stable_s", 8)) * 1000);
  if (want <= 0) {
    if (stable) {
      ready_marked = true;
      write_file(tagged(root_dir() + "\\captures\\auto", ".done"), "ready\n", 6);
      log("[auto] scene ready at present %llu (auto_traces=0: no trace)", (unsigned long long)frame);
    }
    return false;
  }
  if (!stable || frame < g_auto_next_allowed) return false;
  g_auto_next_allowed = frame + uint64_t(cfg_int("auto_interval", 600));
  return true;
}

void trace_on_present(VkQueue queue, const VkPresentInfoKHR* info) {
  uint64_t frame = frame_index();
  uint32_t passes = g_passes.exchange(0, std::memory_order_relaxed);
  uint32_t draws = g_draws.exchange(0, std::memory_order_relaxed);
  if (!g_recording) {
    // The hotkey is a developer tool (dev=1 in the repository's bg3rtx.ini, 0 in
    // the release profile): a player could otherwise fill the disk with traces.
    bool down = cfg_int("dev", 0) && (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
                (GetAsyncKeyState(VK_F10) & 0x8000);
    bool key = down && !g_key_was_down;
    g_key_was_down = down;
    std::string req = tagged(root_dir() + "\\trace", ".request");   // (was "\t...": a tab, so the file never triggered)
    bool file = file_mtime(req) != 0;
    if (file) DeleteFileA(req.c_str());
    bool automatic = auto_trigger(frame, passes, draws);
    if (key || file || automatic) {
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
      log("[trace] recording from present %llu (%s, %u passes last frame)", static_cast<unsigned long long>(frame),
          key ? "hotkey" : file ? "file" : "auto", passes);
    }
    return;
  }
  // Submissions are tagged with the present count at submit time; the frame
  // tagged start+1 is the first whose command buffers were all begun while recording.
  if (frame >= g_start + 2) {
    g_recording = false;
    std::string dir = write_trace(g_start + 1, info);
    dumps_finish(g_start + 1, dir, queue);
    geo_finish(g_start + 1, dir, queue);
    write_file(dir + "\\done.txt", "ok\n", 3);
    int want = cfg_int("auto_traces", 1);
    if (cfg_int("auto_trace", 0) && want > 0 && ++g_auto_done >= uint32_t(want)) {
      std::string marker = tagged(root_dir() + "\\captures\\auto", ".done");
      write_file(marker, "done\n", 5);
      log("[auto] all %u traces taken", g_auto_done);
    }
  }
}

}  // namespace bg3
