#include "abcd/analysis/environment_index.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <random>
#include <vector>

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
}
}  // namespace

int main() {
  using namespace abcd::analysis;
  const EnvironmentRef entry = CapturedDepth{0};
  std::vector<EnvironmentDefinition> definitions{
      {entry, {2, {}}},
      {EnvironmentId{0}, {3, abcd::abc::PoolIndex{201}}},
      {EnvironmentId{0}, {1, {}}}};
  auto index = std::get<EnvironmentIndex>(EnvironmentIndex::Build(definitions));
  Check(index.Size() == 3);
  Check(index.Find({1})->allocation.scope->value == 201);
  Check(index.Find({3}) == nullptr);
  const auto first =
      std::get<LexicalBinding>(index.Resolve(EnvironmentId{1}, {{1}, {1}}));
  const auto sibling =
      std::get<LexicalBinding>(index.Resolve(EnvironmentId{2}, {{1}, {1}}));
  Check(first.environment == EnvironmentRef{EnvironmentId{0}});
  Check(first.environment == sibling.environment);
  Check(std::get<LexicalBinding>(index.Resolve(EnvironmentId{1}, {{0}, {0}}))
            .environment !=
        std::get<LexicalBinding>(index.Resolve(EnvironmentId{2}, {{0}, {0}}))
            .environment);
  Check(std::get<EnvironmentRef>(index.Ancestor(EnvironmentId{1}, {2})) ==
        entry);
  Check(std::get<EnvironmentRef>(index.Ancestor(EnvironmentId{1}, {4})) ==
        EnvironmentRef{CapturedDepth{2}});
  Check(std::get<EnvironmentError>(index.Resolve(
            EnvironmentId{2}, {{0}, {1}})) == EnvironmentError::kSlotRange);
  Check(std::get<EnvironmentError>(index.Ancestor(EnvironmentId{3}, {0})) ==
        EnvironmentError::kInvalidEnvironment);
  Check(std::get<LexicalBinding>(index.Resolve(entry, {{65535}, {65535}}))
            .environment == EnvironmentRef{CapturedDepth{65535}});
  constexpr auto kMaxDepth = std::numeric_limits<std::uint32_t>::max();
  Check(std::get<EnvironmentError>(index.Ancestor(
            CapturedDepth{kMaxDepth}, {1})) == EnvironmentError::kDepthRange);
  // An unused high ancestor must not make a shallow binding unrepresentable.
  definitions = {{CapturedDepth{kMaxDepth}, {1, {}}}};
  auto high = std::get<EnvironmentIndex>(EnvironmentIndex::Build(definitions));
  Check(std::get<EnvironmentRef>(high.Ancestor(EnvironmentId{0}, {1})) ==
        EnvironmentRef{CapturedDepth{kMaxDepth}});
  Check(std::get<EnvironmentError>(high.Ancestor(EnvironmentId{0}, {2})) ==
        EnvironmentError::kDepthRange);
  definitions[0].parent = EnvironmentId{0};
  Check(std::get<EnvironmentError>(EnvironmentIndex::Build(definitions)) ==
        EnvironmentError::kInvalidParent);
  definitions[0].parent = EnvironmentId{UINT32_MAX};
  Check(std::get<EnvironmentError>(EnvironmentIndex::Build(definitions)) ==
        EnvironmentError::kInvalidParent);
  definitions[0].parent = entry;
  const auto bytes = high.AllocatedBytes();
  Check(std::holds_alternative<EnvironmentIndex>(
      EnvironmentIndex::Build(definitions, {bytes})));
  Check(std::get<EnvironmentError>(EnvironmentIndex::Build(
            definitions, {bytes - 1})) == EnvironmentError::kResourceLimit);
  Check(std::holds_alternative<EnvironmentIndex>(
      EnvironmentIndex::Build({}, {0})));
  auto moved = std::move(index);
  Check(index.Size() == 0 && index.AllocatedBytes() == 0 &&
        index.Find({0}) == nullptr);
  Check(std::get<EnvironmentError>(index.Ancestor(EnvironmentId{0}, {0})) ==
        EnvironmentError::kInvalidEnvironment);
  Check(std::get<EnvironmentRef>(moved.Ancestor(EnvironmentId{1}, {1})) ==
        EnvironmentRef{EnvironmentId{0}});
  index = std::move(moved);
  Check(moved.Size() == 0 && moved.Find({0}) == nullptr);
  Check(std::get<EnvironmentRef>(index.Ancestor(EnvironmentId{1}, {1})) ==
        EnvironmentRef{EnvironmentId{0}});
  definitions = {{entry, {0, {}}}, {entry, {0, {}}}};
  auto empty_slots =
      std::get<EnvironmentIndex>(EnvironmentIndex::Build(definitions));
  Check(std::get<EnvironmentError>(empty_slots.Resolve(
            EnvironmentId{0}, {{0}, {0}})) == EnvironmentError::kSlotRange);
  Check(std::get<EnvironmentRef>(empty_slots.Ancestor(EnvironmentId{0}, {0})) !=
        std::get<EnvironmentRef>(empty_slots.Ancestor(EnvironmentId{1}, {0})));

  // Small independent parent-walking oracle, including different entry-chain
  // anchors. Production queries must not use this depth-by-depth algorithm.
  std::minstd_rand random(201);
  constexpr std::uint32_t kOracleNodes = 64;
  constexpr unsigned kOracleRuns = 32;
  for (unsigned run = 0; run < kOracleRuns; ++run) {
    definitions.clear();
    for (std::uint32_t i = 0; i < kOracleNodes; ++i) {
      const EnvironmentRef parent =
          i == 0 || random() % 4 == 0
              ? EnvironmentRef{CapturedDepth{
                    static_cast<std::uint32_t>(random() % 10)}}
              : EnvironmentRef{
                    EnvironmentId{static_cast<std::uint32_t>(random() % i)}};
      definitions.push_back({parent, {1, {}}});
    }
    auto forest =
        std::get<EnvironmentIndex>(EnvironmentIndex::Build(definitions));
    for (std::uint32_t i = 0; i < kOracleNodes; ++i) {
      for (const std::uint16_t depth :
           {0, 1, 2, 3, 7, 15, 31, 63, 255, 65535}) {
        EnvironmentRef expected = EnvironmentId{i};
        std::uint32_t remaining = depth;
        while (remaining != 0) {
          if (auto* capture = std::get_if<CapturedDepth>(&expected)) {
            capture->value += remaining;
            break;
          }
          expected =
              definitions[std::get<EnvironmentId>(expected).value].parent;
          --remaining;
        }
        Check(std::get<EnvironmentRef>(
                  forest.Ancestor(EnvironmentId{i}, {depth})) == expected);
      }
    }
  }

  for (const std::uint32_t count : {1, 200, 201, 4096, 65537}) {
    definitions.clear();
    definitions.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
      definitions.push_back(
          {i == 0 ? entry : EnvironmentRef{EnvironmentId{i - 1}}, {1, {}}});
    auto chain =
        std::get<EnvironmentIndex>(EnvironmentIndex::Build(definitions));
    for (std::uint32_t i = 0; i < count; ++i) {
      constexpr std::uint16_t kQueryDepth = 65535;
      const auto actual = std::get<EnvironmentRef>(
          chain.Ancestor(EnvironmentId{i}, {kQueryDepth}));
      const EnvironmentRef expected =
          i >= kQueryDepth ? EnvironmentRef{EnvironmentId{i - kQueryDepth}}
                           : EnvironmentRef{CapturedDepth{kQueryDepth - i - 1}};
      Check(actual == expected);
      const auto local_distance = static_cast<std::uint16_t>(
          std::min(i, static_cast<std::uint32_t>(kQueryDepth)));
      Check(std::get<EnvironmentRef>(
                chain.Ancestor(EnvironmentId{i}, {local_distance})) ==
            EnvironmentRef{EnvironmentId{i - local_distance}});
    }
    Check(chain.AllocatedBytes() == count * bytes);
  }
}
