// bg3rtx_hairconv core against the Python reference (tools/hair_groom/hcap.py fixture): tests/fixtures/hairconv has
// cards.hcap (synthetic cards and mask, no game data), the reference record cards.json and the reference v3 groom.
// The native pipeline must give the same verdict, strand and segment counts and IoU/coverage/radiance (3 decimals),
// and a v3 groom whose decoded LODs equal the reference's: triangles, roots and segments exact, barycentrics within
// half a quantisation step, radii within the 16-bit log step.
#include "../tools/hairconv/hairconv.h"
#include "../common/hair_groom_v3.h"
#include "../common/hair_groom_lzms.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace hairconv;
namespace hg = bg3::hair_groom;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

static std::string text(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static double field(const std::string& j, const std::string& key, int index = -1) {
  size_t at = j.find("\"" + key + "\":");
  CHECK(at != std::string::npos);
  at = j.find(':', at) + 1;
  if (index >= 0) {
    at = j.find('[', at) + 1;
    for (int i = 0; i < index; ++i) at = j.find(',', at) + 1;
  }
  return std::strtod(j.c_str() + at + strspn(j.c_str() + at, " \n"), nullptr);
}

int main(int argc, char** argv) {
  CHECK(argc >= 2);
  const std::filesystem::path dir = argv[1];
  Input in;
  std::string error;
  CHECK(read_hcap((dir / "cards.hcap").string(), in, &error));
  const std::string ref = text(dir / "cards.json");
  int grid[2];
  double fit = 0;
  detect_tile_grid(in.mesh, grid, &fit);
  CHECK(grid[0] == int(field(ref, "tile_grid", 0)) && grid[1] == int(field(ref, "tile_grid", 1)));
  Report rep;
  Groom g = convert(in.mesh, in.mask, grid, rep, 4);
  int dropped = 0;
  g = dedupe(g, &dropped, 4);
  CHECK(int(g.strands()) == int(field(ref, "strands")) && int(g.points_count() - g.strands()) == int(field(ref, "segments")));
  if (dropped != int(field(ref, "dedupe_dropped")) || rep.cards != int(field(ref, "cards")))
    std::printf("dropped %d (ref %g), cards %d (ref %g)\n", dropped, field(ref, "dedupe_dropped"), rep.cards, field(ref, "cards"));
  CHECK(dropped == int(field(ref, "dedupe_dropped")) && rep.cards == int(field(ref, "cards")));
  Validation v = validate_full(in.mesh, in.mask, g, in.head, 4);
  for (int k = 0; k < 3; ++k) {
    CHECK(std::fabs(std::nearbyint(v.iou[k] * 1000) / 1000 - field(ref, "iou", k)) < 1e-9);
    CHECK(std::fabs(std::nearbyint(v.coverage[k] * 1000) / 1000 - field(ref, "coverage_delta", k)) < 1e-9);
    CHECK(std::fabs(std::nearbyint(v.radiance[k] * 1000) / 1000 - field(ref, "radiance_delta", k)) < 1e-9);
  }
  // The v3 groom against the reference file (written by the fixture whatever the verdict).
  char name[32];
  std::snprintf(name, sizeof name, "%016llx.hgrm", (unsigned long long)index_id(in.mesh));
  const std::string want = text(dir / name);
  CHECK(!want.empty());
  const std::filesystem::path out = std::filesystem::temp_directory_path() / ("hairconv_test_" + std::string(name));
  CHECK(write_v3(out.string(), in.mesh, g, &error) > 0);
  const std::string got = text(out);
  std::filesystem::remove(out);
  hg::Asset ha, hb;
  CHECK(hg::parse_header((const uint8_t*)got.data(), got.size(), ha) && hg::parse_header((const uint8_t*)want.data(), want.size(), hb));
  CHECK(ha.index_id == hb.index_id && ha.position_id == hb.position_id && ha.lod_count == 3);
  for (uint32_t l = 0; l < 3; ++l) {
    std::vector<uint8_t> a, b;
    CHECK(hg::decode_v3_lod((const uint8_t*)got.data(), got.size(), l, hg::lzms_inflate, a));
    CHECK(hg::decode_v3_lod((const uint8_t*)want.data(), want.size(), l, hg::lzms_inflate, b));
    hg::Lod la, lb;
    uint64_t at = 0, bt = 0;
    CHECK(hg::parse_lod(a.data(), a.size(), at, ha.indices / 3, la) && hg::parse_lod(b.data(), b.size(), bt, hb.indices / 3, lb));
    CHECK(la.points == lb.points && la.segments == lb.segments);
    for (uint32_t i = 0; i < la.points; ++i) {
      CHECK(la.triangle[i] == lb.triangle[i] && la.root[i] == lb.root[i]);
      CHECK(std::fabs(la.bary[2 * i] - lb.bary[2 * i]) < 2e-5 && std::fabs(la.bary[2 * i + 1] - lb.bary[2 * i + 1]) < 2e-5);
      CHECK(std::fabs(la.radius[i] / lb.radius[i] - 1) < 4e-4);
    }
    for (uint32_t i = 0; i < la.segments; ++i) CHECK(la.segment[i] == lb.segment[i]);
  }
  std::printf("hairconv: fixture matches the Python reference (%u strands, grid %dx%d)\n", g.strands(), grid[0], grid[1]);
  return 0;
}
