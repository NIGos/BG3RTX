#pragma once
// The hair and beard catalogue built from the game's packages (docs/RTX_HAIR_ROADMAP.md 2f), as the Python batch
// builds it from mesh_materials (tools/assets/mesh_materials.py collect + tools/hair_groom/batch.py styles) and orders
// it (tools/hair_groom/reach.py categories, tools/hair_groom/prepare.py tiers).
#include "pak.h"

#include <functional>
#include <string>
#include <vector>

namespace hairconv::catalogue {

struct Style {
  std::string name;     // "<GR2 stem>:<mesh>" (batch.py)
  std::string gr2, mesh, mask, visual;
  int lod = 0;
  std::string category; // reach.py: player, companion, named NPC, generic NPC, hat variant, item, CV only, unused
  int tier = 5;         // prepare.py: 1 companion, 2 player, 3 named, 4 hat, 5 other, 6 unused
};
using Log = std::function<void(const std::string&)>;

// Every LOD0 / LOD1 object of a VisualBank resource whose material uses CHAR_Hair, one per (GR2, mesh), with its
// ID_Depth_Root_Alpha_MSKA texture; sorted by name. Reads every */Content/*.lsf (read-only).
bool styles(const pak::Set& ps, std::vector<Style>& out, const Log& log, std::string* error);
// reach.py categories and prepare.py tiers (character creation, character visuals, templates; read-only). On failure
// every style stays "other" (it only orders the work).
void classify(const pak::Set& ps, std::vector<Style>& styles, const Log& log);
int tier_of(const std::string& category);
extern const char* const kTierNames[7];   // "seen", "companion", "player", "named", "hat", "other", "unused"
// Conversion order: (tier, LOD, name).
void order(std::vector<Style>& styles);

// <file>: the catalogue keyed by the packages' fingerprint (names, sizes, write times), so a second run skips the
// LSF reads. load() is false when the file is missing or the packages changed.
bool load(const std::string& file, const std::string& fingerprint, std::vector<Style>& out);
bool save(const std::string& file, const std::string& fingerprint, const std::vector<Style>& styles);

}  // namespace hairconv::catalogue
