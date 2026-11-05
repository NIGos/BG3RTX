#include "../tools/dlss5_fixture_logic.h"

#include <cstdio>
#include <limits>

namespace {
int failures{};
void check(bool condition, const char* message) {
  std::printf("%s %s\n", condition ? "PASS" : "FAIL", message);
  failures += !condition;
}
}

int main() {
  using namespace bg3::dlss5;
  const Luid luid{{0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef}};
  check(same_luid(luid, true, luid), "valid identical LUID matches");
  check(!same_luid(luid, false, luid), "invalid Vulkan LUID cannot match");
  check(!same_luid({}, false, {}), "zero invalid LUID cannot select an adapter");
  for (std::size_t byte = 0; byte < luid.size(); ++byte) {
    auto different = luid;
    different[byte] ^= 0x80;
    check(!same_luid(luid, true, different), "each LUID byte participates in adapter matching");
  }

  FenceBook book;
  check(book.completed() == 0, "new slot starts at fence zero");
  check(!book.complete(2), "completion without a submission is rejected");
  const auto first = book.begin();
  check(first && first->ready == 1 && first->done == 2, "first producer and consumer values are 1 and 2");
  check(!book.begin(), "in-flight slot cannot reserve overlapping values");
  check(!book.complete(0) && !book.complete(1), "stale and producer-only completion cannot retire the slot");
  check(book.completed() == 0 && !book.begin(), "early completion leaves ownership in flight");
  check(book.complete(2) && book.completed() == 2, "observed consumer completion retires the slot");
  check(!book.complete(2), "duplicate completion cannot retire another submission");
  const auto second = book.begin();
  check(second && second->ready == 3 && second->done == 4, "reused slot reserves strictly increasing values");
  check(!book.complete(2), "old frame completion cannot retire reused slot");
  check(!book.complete(100) && book.completed() == 2, "unexpected higher value cannot cause future signals below the timeline");
  check(!book.begin(), "unexpected higher value leaves the slot in flight");
  check(book.complete(4) && book.completed() == 4, "only the reserved consumer completion retires the slot");
  const auto third = book.begin();
  check(third && third->ready == 5 && third->done == 6, "normal completion permits the next monotonic pair");
  check(book.complete(6), "third consumer completion retires normally");

  constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
  FenceBook edge(maximum - 2);
  const auto final = edge.begin();
  check(final && final->ready == maximum - 1 && final->done == maximum,
        "last representable pair does not wrap");
  check(edge.complete(maximum) && !edge.begin(), "exhausted fence refuses a wrapped submission");
  FenceBook one_left(maximum - 1);
  check(!one_left.begin() && one_left.completed() == maximum - 1, "one remaining value is insufficient");
  FenceBook none_left(maximum);
  check(!none_left.begin(), "fully exhausted fence refuses a submission");
  return failures ? 1 : 0;
}
