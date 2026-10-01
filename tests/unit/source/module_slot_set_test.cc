#include "../../../src/source/module_slot_set.h"
#include "analysis/ssa_fixture.h"

#include <limits>
#include <thread>

int main() {
  using namespace abcd;
  using source::ModuleSlotSet;
  constexpr auto kMaximumSlot = std::numeric_limits<decltype(analysis::ModuleSlot::value)>::max();
  constexpr std::size_t kStorageBytes = 8'000'000;
  for (const auto count : {1U, 200U, 201U, 400U, 800U}) {
    ModuleSlotSet slots;
    std::size_t bytes = kStorageBytes;
    const auto before = bytes;
    std::vector<ModuleSlotSet> prefixes;
    for (std::uint32_t i = 0; i < count; ++i) {
      prefixes.push_back(slots);
      slots = slots.With({static_cast<std::uint16_t>(i)}, bytes);
    }
    for (std::uint32_t i = 0; i < count; ++i) {
      test::Check(slots.Contains({static_cast<std::uint16_t>(i)}));
      test::Check(!prefixes[i].Contains({static_cast<std::uint16_t>(i)}));
      if (i) test::Check(prefixes[i].Contains({static_cast<std::uint16_t>(i - 1)}));
    }
    test::Check(!slots.Contains({kMaximumSlot}) && bytes < before);
    std::size_t zero = 0;
    test::Check(slots.With({0}, zero).Same(slots));
    bool failed = false;
    try { (void)slots.With({kMaximumSlot}, zero); }
    catch (const std::length_error&) { failed = true; }
    test::Check(failed && !slots.Contains({kMaximumSlot}));
    const auto sparse = slots.With({kMaximumSlot}, bytes);
    test::Check(sparse.Contains({kMaximumSlot}) && sparse.Contains({0}) && !sparse.Same(slots));
  }
  // Shared roots are immutable; allocation admission is thread-local and no
  // retained allocator refers to a stack quota during destruction elsewhere.
  std::size_t bytes = kStorageBytes;
  const auto base = ModuleSlotSet{}.With({kMaximumSlot}, bytes);
  std::array<ModuleSlotSet, 2> results;
  const auto run = [&](unsigned branch) {
    std::size_t remaining = kStorageBytes;
    auto slots = base;
    for (std::uint16_t i = 0; i < 400; ++i)
      slots = slots.With({static_cast<std::uint16_t>(2 * i + branch)}, remaining);
    results[branch] = std::move(slots);
  };
  std::thread zero(run, 0), one(run, 1);
  zero.join(); one.join();
  for (std::uint16_t i = 0; i < 800; ++i) {
    test::Check(results[i % 2].Contains({i}));
    test::Check(!results[1 - i % 2].Contains({i}));
  }
  test::Check(base.Contains({kMaximumSlot}) && !base.Contains({0}));
}
