#pragma once
// bg3rtx_hairconv: the native strand groom converter (docs/RTX_HAIR_ROADMAP.md 2d). A port of the Python reference
// (tools/hair_groom: convert.py, strands2d.py, curves2d.py, geom.py, analysis.py, render.py, batch.py verdicts,
// asset.py / storage.py v3 writer) that reads an .hcap (tools/hair_groom/hcap.py layout) and writes an .hgrm v3 the
// layer reads (common/hair_groom_v3.h). Same arithmetic order as the reference where it decides a discrete outcome
// (locate, run cuts, dedupe pairs); doubles where numpy uses float64.
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace hairconv {

struct Mesh {
  std::vector<float> pos;      // [V * 3]
  std::vector<float> uv;       // [V * 2]
  std::vector<uint32_t> tris;  // [T * 3] native vertex numbering
  uint32_t vertices() const { return uint32_t(pos.size() / 3); }
  uint32_t triangles() const { return uint32_t(tris.size() / 3); }
};

struct Mask {
  int w = 0, h = 0;
  std::vector<uint8_t> rgba;   // [h * w * 4]: R strand id, G depth, B root, A coverage
  uint8_t at(int x, int y, int c) const { return rgba[(size_t(y) * w + x) * 4 + c]; }
};

struct Head {
  double centre[3] = {0, 1e4, 0}, radii[3] = {1e-3, 1e-3, 1e-3};
  bool valid() const { return radii[0] > 0.01; }
};

struct Input {
  Mesh mesh;
  Mask mask;
  bool has_head = false, has_grid = false;
  Head head;
  int grid[2] = {0, 0};
  uint64_t material = 0;
};
bool read_hcap(const std::string& path, Input& out, std::string* error);

// convert.Groom without joints/weights/normal_offset (the v3 file does not store them).
struct Groom {
  std::vector<float> points;     // [P * 3]
  std::vector<float> radii;      // [P]
  std::vector<uint32_t> offsets; // [S + 1]
  std::vector<int32_t> card;     // [S]
  std::vector<float> root;       // [P] root_param 0..1
  std::vector<uint32_t> triangle;// [P]
  std::vector<float> bary;       // [P * 3]
  uint32_t strands() const { return offsets.empty() ? 0 : uint32_t(offsets.size() - 1); }
  uint32_t points_count() const { return uint32_t(radii.size()); }
};

struct TileStat { int strands = 0; double typical_px = 0, recall = 0; };

struct Report {
  int cards = 0, triangles = 0, cards_spanning_tiles = 0;
  long long samples = 0, unmapped_samples = 0, uv_overlap_samples = 0;
  int mixed_winding_cards = 0, strands_without_width = 0;
  std::string tracer = "rows";
  std::vector<double> strand_root_corr;         // 2-D tracer only
  std::map<std::pair<int, int>, TileStat> tiles;
};

// ---- geometry (geom.py, convert.py) ----
// Card per triangle: connected components over shared vertices, labels ordered by smallest vertex (np.unique).
int cards(const Mesh& m, std::vector<int32_t>& card_of);
// detect_tile_grid: (columns, rows) and the share of cards inside one cell.
void detect_tile_grid(const Mesh& m, int grid[2], double* fit);
// geom.pairs_within: unordered index pairs closer than `distance` (spatial hash).
std::vector<std::pair<uint32_t, uint32_t>> pairs_within(const float* points, size_t n, double distance, int threads);

// ---- tracing ----
// One sampled strand row: atlas UV, width (UV u units), slope (px per row), root and depth channel 0..1, and for the
// 2-D tracer the unit width direction in pixels (nx, ny); empty for the row tracer.
struct Row { std::vector<double> u, v, wd, slope, root, depth, nx, ny; };
// tile_strands (strands2d.trace / resample per tile): rows per tile index ty * gx + tx.
void tile_strands(const Mask& mask, const int grid[2], int step, int threshold, std::vector<std::vector<Row>>& per_tile,
                  std::map<std::pair<int, int>, TileStat>& stats, int threads);
// curves2d (tools/hairconv/curves2d.cpp): the median over tiles of the root/row correlation (false: no tile qualifies).
bool root_row_correlation(const Mask& mask, const int grid[2], int threshold, double* corr);
// convert.curve_strands: rows, their UV boxes (lo/hi: 2 per row), per-tile stats and per-tile strand root correlation.
void curve_strands(const Mask& mask, const int grid[2], int step, int threshold, std::vector<Row>& rows,
                   std::vector<double>& lo, std::vector<double>& hi, std::map<std::pair<int, int>, TileStat>& stats,
                   std::vector<double>& corr, int threads);

// ---- conversion ----
// convert.convert(mesh, mask, tile_grid, tracer="auto"), then convert.dedupe.
Groom convert(const Mesh& m, const Mask& mask, const int grid[2], Report& rep, int threads);
Groom dedupe(const Groom& g, int* dropped, int threads, double distance = 0.0005, double share = 0.8);
Groom thin(const Groom& g, float keep);

// ---- analysis.py ----
std::vector<double> root_channel_check(const Mask& mask, const int grid[2], int threshold = 128);
struct Orientation { int strands_reversed = 0, strands_decided = 0, cards_mostly_reversed = 0, cards_decided = 0; };
Orientation orientation(const Groom& g, const double centre[3], double margin = 0.005);
Groom reorient(const Groom& g, const double centre[3], int* flipped);
Head head_from_roots(const Groom& g);   // capture.head_from_roots

// ---- validation (render.py, batch.process; tools/hairconv/validate.cpp) ----
struct Validation { std::vector<double> iou, coverage, radiance; };
Validation validate_full(const Mesh& m, const Mask& mask, const Groom& g, const Head& head, int threads);

// ---- output (asset.py / storage.py) ----
// uint64 ids of layer/mesh_content_id.h v1 (positions) and common/hair_groom_asset.h (index only).
uint64_t content_id(const Mesh& m);
uint64_t index_id(const Mesh& m);
// .hgrm v3 of LOD 0 + the keeps {1, 0.5, 0.25}; returns the barycentric bits (0 on failure).
int write_v3(const std::string& path, const Mesh& m, const Groom& lod0, std::string* error);

}  // namespace hairconv
