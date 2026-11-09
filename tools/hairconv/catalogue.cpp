// Hair catalogue from the packages (catalogue.h): mesh_materials.collect + batch.styles, reach.references +
// reach.category, prepare.tier / order.
#include "catalogue.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace hairconv::catalogue {

const char* const kTierNames[7] = {"seen", "companion", "player", "named", "hat", "other", "unused"};

namespace {
using Clock = std::chrono::steady_clock;
double secs(Clock::time_point a) { return std::chrono::duration<double>(Clock::now() - a).count(); }
std::string lower(std::string s) {
  for (char& c : s) c = char(tolower(static_cast<unsigned char>(c)));
  return s;
}
bool ends_with(const std::string& s, const char* suf) {
  const size_t n = strlen(suf);
  return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}
std::string slashes(std::string s) { std::replace(s.begin(), s.end(), '\\', '/'); return s; }

// Runs f(i) for i in [0, n) on a few threads, with a progress line every 30 s.
template <class F> void parallel(size_t n, const char* what, const Log& log, F&& f) {
  std::atomic<size_t> next{0}, done{0};
  const auto start = Clock::now();
  auto last = start;
  std::mutex mu;
  const unsigned threads = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
  std::vector<std::thread> pool;
  for (unsigned t = 0; t < threads; ++t)
    pool.emplace_back([&] {
      for (size_t i = next++; i < n; i = next++) {
        f(i);
        const size_t d = ++done;
        std::lock_guard<std::mutex> lock(mu);
        if (secs(last) >= 30 && log) {
          const double rate = d / std::max(1e-3, secs(start));
          char b[200];
          snprintf(b, sizeof b, "[hairconv all] %s %zu/%zu, %.0f/s, ETA %.0fs", what, d, n, rate, (n - d) / std::max(rate, 1e-9));
          log(b);
          last = Clock::now();
        }
      }
    });
  for (auto& t : pool) t.join();
}

struct Obj { std::string object_id, material_id; int lod = 0; };
struct Visual { std::string id, source; std::vector<Obj> objects; };
struct Material { std::string id, source, mask_texture; };
struct Texture { std::string id, source; };
struct Banks { std::vector<Visual> visuals; std::vector<Material> materials; std::vector<Texture> textures; bool failed = false; };

// batch.styles mask_id: the first Texture2DParameters named ID_Depth_Root_Alpha_MSKA in the Python dict walk's
// order (a stack: the last child's subtree is searched first).
std::string mask_texture(const pak::Resource& r, int material) {
  std::vector<int> stack{material};
  while (!stack.empty()) {
    const int i = stack.back();
    stack.pop_back();
    const pak::Node& n = r.nodes[size_t(i)];
    if (n.name == "Texture2DParameters" && n.str("ParameterName") == "ID_Depth_Root_Alpha_MSKA") return n.str("ID");
    for (int c : n.children) stack.push_back(c);
  }
  return {};
}

Banks read_banks(const pak::Resource& r) {
  Banks b;
  for (int root : r.roots) {
    const std::string& region = r.nodes[size_t(root)].name;
    const bool vb = region == "VisualBank", mb = region == "MaterialBank",
               tb = region == "TextureBank" || region == "VirtualTextureBank";
    if (!vb && !mb && !tb) continue;
    for (int c : r.nodes[size_t(root)].children) {
      const pak::Node& n = r.nodes[size_t(c)];
      const std::string id = n.str("ID");
      if (vb) {
        Visual v{id, slashes(n.str("SourceFile")), {}};
        for (int o : n.children) {
          const pak::Node& on = r.nodes[size_t(o)];
          if (on.name != "Objects") continue;
          const pak::Attr* lod = on.get("LOD");
          v.objects.push_back({on.str("ObjectID"), on.str("MaterialID"), lod ? int(lod->num) : 0});
        }
        b.visuals.push_back(std::move(v));
      } else if (mb) {
        b.materials.push_back({id, slashes(n.str("SourceFile")), mask_texture(r, c)});
      } else {
        b.textures.push_back({id, n.str("SourceFile")});
      }
    }
  }
  return b;
}

bool parse_any(const std::string& name, const std::vector<uint8_t>& raw, pak::Resource& r, std::string* error) {
  return ends_with(name, ".lsx") ? pak::parse_lsx(raw.data(), raw.size(), r, error) : pak::parse_lsf(raw.data(), raw.size(), r, error);
}
}  // namespace

bool styles(const pak::Set& ps, std::vector<Style>& out, const Log& log, std::string* error) {
  out.clear();
  std::vector<const std::string*> names;
  for (const std::string& n : ps.names())
    if (n.find("/Content/") != std::string::npos && ends_with(n, ".lsf")) names.push_back(&n);
  if (names.empty()) { if (error) *error = "no */Content/*.lsf in the packages"; return false; }
  const auto start = Clock::now();
  std::vector<Banks> banks(names.size());
  std::atomic<int> failures{0};
  parallel(names.size(), "content files", log, [&](size_t i) {
    std::vector<uint8_t> raw;
    pak::Resource r;
    std::string e;
    if (!ps.read(*names[i], raw, &e) || !pak::parse_lsf(raw.data(), raw.size(), r, &e)) { banks[i].failed = true; ++failures; return; }
    banks[i] = read_banks(r);
  });
  // collect(): the first value of a repeated ID is kept (sorted path order).
  std::unordered_map<std::string, const Visual*> visuals;
  std::unordered_map<std::string, const Material*> materials;
  std::unordered_map<std::string, const Texture*> textures;
  for (const Banks& b : banks) {
    for (const Visual& v : b.visuals) visuals.emplace(v.id, &v);
    for (const Material& m : b.materials) materials.emplace(m.id, &m);
    for (const Texture& t : b.textures) textures.emplace(t.id, &t);
  }
  std::vector<const Visual*> sorted;
  for (auto& [id, v] : visuals) sorted.push_back(v);
  std::sort(sorted.begin(), sorted.end(), [](const Visual* a, const Visual* b) { return a->id < b->id; });
  std::set<std::pair<std::string, std::string>> seen;
  for (const Visual* v : sorted)
    for (const Obj& o : v->objects) {
      if (o.lod > 1) continue;
      const auto m = materials.find(o.material_id);
      if (m == materials.end() || !ends_with(m->second->source, "/CHAR_Hair.lsf")) continue;
      // ObjectID "<visual>.<mesh>.<n>": the mesh is the middle part.
      std::vector<std::string> parts;
      size_t a = 0;
      for (;;) {
        const size_t d = o.object_id.find('.', a);
        parts.push_back(o.object_id.substr(a, d == std::string::npos ? std::string::npos : d - a));
        if (d == std::string::npos) break;
        a = d + 1;
      }
      const std::string mesh = parts.size() >= 3 ? parts[1] : std::string();
      if (mesh.empty() || seen.count({v->source, mesh})) continue;
      seen.insert({v->source, mesh});
      Style s;
      const size_t slash = v->source.find_last_of('/');
      std::string stem = slash == std::string::npos ? v->source : v->source.substr(slash + 1);
      const size_t dot = stem.find_last_of('.');
      if (dot != std::string::npos && dot > 0) stem = stem.substr(0, dot);
      s.name = stem + ":" + mesh;
      s.gr2 = v->source;
      s.mesh = mesh;
      s.lod = o.lod;
      s.visual = v->id;
      const auto t = textures.find(m->second->mask_texture);
      s.mask = t == textures.end() ? std::string() : slashes(t->second->source);
      out.push_back(std::move(s));
    }
  std::stable_sort(out.begin(), out.end(), [](const Style& a, const Style& b) { return a.name < b.name; });
  if (log) {
    char b[200];
    snprintf(b, sizeof b, "[hairconv all] catalogue: %zu hair and beard rows from %zu content files in %.1fs (%d unreadable)",
             out.size(), names.size(), secs(start), failures.load());
    log(b);
  }
  return !out.empty() || (error && (*error = "no CHAR_Hair rows in the catalogue", false));
}

int tier_of(const std::string& c) {
  if (c == "companion") return 1;
  if (c == "player") return 2;
  if (c == "named NPC") return 3;
  if (c == "hat variant") return 4;
  if (c == "CV only" || c == "unused") return 6;
  return 5;   // generic NPC, item, unknown
}

void classify(const pak::Set& ps, std::vector<Style>& styles, const Log& log) {
  struct Refs { bool cc = false, hat = false, item = false; std::vector<std::string> cv; };
  std::unordered_map<std::string, Refs> refs;
  for (const Style& s : styles) refs[lower(s.visual)];
  std::vector<const std::string*> cc, cvf, tpl;
  for (const std::string& n : ps.names()) {
    if (n.find("/CharacterCreation/CharacterCreation") != std::string::npos && ends_with(n, "Visuals.lsx")) cc.push_back(&n);
    if (n.find("[PAK]_CharacterVisuals/_merged.lsf") != std::string::npos) cvf.push_back(&n);
    if (ends_with(n, "RootTemplates/_merged.lsf") || ends_with(n, "/Characters/_merged.lsf")) tpl.push_back(&n);
  }
  const auto start = Clock::now();
  std::mutex mu;
  std::unordered_map<std::string, std::unordered_set<std::string>> users;   // character visual -> template names
  std::atomic<int> failures{0};
  std::vector<const std::string*> all;
  all.insert(all.end(), cc.begin(), cc.end());
  all.insert(all.end(), cvf.begin(), cvf.end());
  all.insert(all.end(), tpl.begin(), tpl.end());
  parallel(all.size(), "classification files", log, [&](size_t i) {
    const std::string& name = *all[i];
    std::vector<uint8_t> raw;
    pak::Resource r;
    std::string e;
    if (!ps.read(name, raw, &e) || !parse_any(name, raw, r, &e)) { ++failures; return; }
    raw.clear();
    raw.shrink_to_fit();
    std::vector<std::string> cc_hits, item_hits, hat_hits;
    std::vector<std::pair<std::string, std::string>> cv_hits, user_hits;
    if (i < cc.size()) {
      for (int root : r.roots)
        r.walk(root, [&](int n) {
          const std::string v = lower(r.nodes[size_t(n)].str("VisualResource"));
          if (refs.count(v)) cc_hits.push_back(v);
        });
    } else if (i < cc.size() + cvf.size()) {
      for (int root : r.roots)
        r.walk(root, [&](int n) {
          if (r.nodes[size_t(n)].name != "Resource") return;
          const std::string id = lower(r.nodes[size_t(n)].str("ID"));
          for (int c : r.nodes[size_t(n)].children) {
            if (r.nodes[size_t(c)].name != "Slots") continue;
            const std::string v = lower(r.nodes[size_t(c)].str("VisualResource"));
            if (refs.count(v)) cv_hits.push_back({v, id});
          }
        });
    } else {
      for (int root : r.roots)
        r.walk(root, [&](int n) {
          const pak::Node& go = r.nodes[size_t(n)];
          if (go.name != "GameObjects") return;
          const std::string tname = go.str("Name");
          const std::string cv = lower(go.str("CharacterVisualResourceID"));
          if (!cv.empty()) user_hits.push_back({cv, tname});
          const std::string vt = lower(go.str("VisualTemplate"));
          if (refs.count(vt)) item_hits.push_back(vt);
          const int eq = r.child(n, "Equipment");
          if (eq < 0) return;
          for (int hair_type : r.nodes[size_t(eq)].children)
            r.walk(hair_type, [&](int o) {
              std::string v = r.nodes[size_t(o)].str("MapValue");
              if (v.empty()) v = r.nodes[size_t(o)].str("Object");
              v = lower(v);
              if (refs.count(v)) hat_hits.push_back(v);
            });
        });
    }
    std::lock_guard<std::mutex> lock(mu);
    for (auto& v : cc_hits) refs[v].cc = true;
    for (auto& [v, id] : cv_hits) refs[v].cv.push_back(id);
    for (auto& v : item_hits) refs[v].item = true;
    for (auto& v : hat_hits) refs[v].hat = true;
    for (auto& [cv, t] : user_hits) users[cv].insert(t);
  });
  for (Style& s : styles) {
    const Refs& r = refs[lower(s.visual)];
    std::string c;
    bool companion = false, named = false, any = false;
    for (const std::string& cv : r.cv) {
      const auto it = users.find(cv);
      if (it == users.end()) continue;
      for (const std::string& t : it->second) {
        any = true;
        companion |= t.rfind("S_Player_", 0) == 0 || t.find("Origin") != std::string::npos;
        named |= t.rfind("S_", 0) == 0;
      }
    }
    if (r.cc) c = "player";
    else if (companion) c = "companion";
    else if (named) c = "named NPC";
    else if (any) c = "generic NPC";
    else if (r.hat) c = "hat variant";
    else if (r.item) c = "item";
    else if (!r.cv.empty()) c = "CV only";
    else c = "unused";
    s.category = c;
    s.tier = tier_of(c);
  }
  if (log) {
    int per[7] = {};
    for (const Style& s : styles) ++per[s.tier];
    char b[240];
    snprintf(b, sizeof b, "[hairconv all] classified %zu styles from %zu files in %.1fs (%d unreadable): companion %d, player %d, "
             "named %d, hat %d, other %d, unused %d", styles.size(), all.size(), secs(start), failures.load(), per[1], per[2], per[3],
             per[4], per[5], per[6]);
    log(b);
  }
}

void order(std::vector<Style>& styles) {
  std::stable_sort(styles.begin(), styles.end(), [](const Style& a, const Style& b) {
    if (a.tier != b.tier) return a.tier < b.tier;
    if (a.lod != b.lod) return a.lod < b.lod;
    return a.name < b.name;
  });
}

namespace {
constexpr const char* kCatalogueVersion = "hair-catalogue-1";
std::vector<std::string> split_tabs(const std::string& line) {
  std::vector<std::string> f;
  size_t a = 0;
  for (;;) {
    const size_t b = line.find('\t', a);
    f.push_back(line.substr(a, b == std::string::npos ? std::string::npos : b - a));
    if (b == std::string::npos) break;
    a = b + 1;
  }
  return f;
}
}  // namespace

bool load(const std::string& file, const std::string& fingerprint, std::vector<Style>& out) {
  out.clear();
  FILE* f = nullptr;
  if (fopen_s(&f, file.c_str(), "rb") != 0 || !f) return false;
  std::string text;
  char buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
  fclose(f);
  size_t at = text.find('\n');
  if (at == std::string::npos || text.substr(0, at) != std::string("# ") + kCatalogueVersion + " " + fingerprint) return false;
  ++at;
  while (at < text.size()) {
    const size_t nl = text.find('\n', at);
    const std::string line = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
    at = nl == std::string::npos ? text.size() : nl + 1;
    const auto fl = split_tabs(line);
    if (fl.size() != 8) { out.clear(); return false; }
    Style s;
    s.name = fl[0]; s.gr2 = fl[1]; s.mesh = fl[2]; s.lod = atoi(fl[3].c_str()); s.mask = fl[4]; s.visual = fl[5];
    s.category = fl[6]; s.tier = atoi(fl[7].c_str());
    out.push_back(std::move(s));
  }
  return !out.empty();
}

bool save(const std::string& file, const std::string& fingerprint, const std::vector<Style>& styles) {
  std::string text = std::string("# ") + kCatalogueVersion + " " + fingerprint + "\n";
  for (const Style& s : styles)
    text += s.name + "\t" + s.gr2 + "\t" + s.mesh + "\t" + std::to_string(s.lod) + "\t" + s.mask + "\t" + s.visual + "\t" + s.category +
            "\t" + std::to_string(s.tier) + "\n";
  const std::string tmp = file + "." + std::to_string(GetCurrentProcessId()) + ".tmp";
  FILE* f = nullptr;
  if (fopen_s(&f, tmp.c_str(), "wb") != 0 || !f) return false;
  const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
  fclose(f);
  return ok && MoveFileExA(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING);
}

}  // namespace hairconv::catalogue
