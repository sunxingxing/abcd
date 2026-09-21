#include <cstdlib>
#include <random>

#include "abcd/analysis/creation_binding.h"

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
}  // namespace
int main() {
  using namespace abcd::analysis;
  std::vector<EnvironmentDefinition> definitions{{CapturedDepth{2}, {3, {}}},
                                                 {EnvironmentId{0}, {2, {}}},
                                                 {CapturedDepth{5}, {1, {}}}};
  const auto caller =
      std::get<EnvironmentIndex>(EnvironmentIndex::Build(definitions));
  const CreationCapture first = OrdinaryCapture{EnvironmentId{1}};
  const CreationCapture second = OrdinaryCapture{EnvironmentId{2}};
  const auto bound = [&](const CreationCapture& context, std::uint32_t depth,
                         std::uint16_t slot) {
    return RebaseCapture(caller, context, {{depth}, {slot}});
  };
  Check(std::get<LexicalBinding>(bound(first, 0, 1)).environment ==
        EnvironmentRef{EnvironmentId{1}});
  const auto outer = std::get<LexicalBinding>(bound(first, 1, 2));
  Check(outer.environment == EnvironmentRef{EnvironmentId{0}} &&
        outer.slot.value == 2);
  Check(std::get<LexicalBinding>(bound(first, 2, 0)).environment ==
        EnvironmentRef{CapturedDepth{2}});
  Check(std::get<LexicalBinding>(bound(second, 0, 0)).environment ==
        EnvironmentRef{EnvironmentId{2}});
  Check(std::get<LexicalBinding>(bound(second, 1, 0)).environment ==
        EnvironmentRef{CapturedDepth{5}});
  Check(std::get<EnvironmentError>(bound(second, 0, 1)) ==
        EnvironmentError::kSlotRange);
  Check(std::get<LexicalBinding>(bound(first, UINT32_MAX, 0)).environment ==
        EnvironmentRef{CapturedDepth{UINT32_MAX}});
  Check(std::get<EnvironmentError>(bound(second, UINT32_MAX, 0)) ==
        EnvironmentError::kDepthRange);
  Check(std::get<CaptureRebaseError>(bound(SendableCapture{}, 0, 0)) ==
        CaptureRebaseError::kSendableEnvironment);
  Check(std::get<EnvironmentError>(
            bound(OrdinaryCapture{EnvironmentId{99}}, 0, 0)) ==
        EnvironmentError::kInvalidEnvironment);
  Check(std::get<EnvironmentError>(
            bound(OrdinaryCapture{CapturedDepth{UINT32_MAX}}, 1, 0)) ==
        EnvironmentError::kDepthRange);
  Check(std::get<LexicalBinding>(
            bound(OrdinaryCapture{CapturedDepth{0}}, UINT32_MAX, UINT16_MAX))
            .environment == EnvironmentRef{CapturedDepth{UINT32_MAX}});

  // Rebase an unresolved capture again through the grandparent's creation.
  const auto pending = std::get<LexicalBinding>(bound(first, 3, 1));
  const std::vector<EnvironmentDefinition> grand_definitions{
      {CapturedDepth{7}, {2, {}}}};
  const auto grandparent =
      std::get<EnvironmentIndex>(EnvironmentIndex::Build(grand_definitions));
  const auto grand = std::get<LexicalBinding>(RebaseCapture(
      grandparent, OrdinaryCapture{EnvironmentId{0}},
      {std::get<CapturedDepth>(pending.environment), pending.slot}));
  Check(grand.environment == EnvironmentRef{CapturedDepth{9}} &&
        grand.slot.value == 1);

  // Independent small parent-walk oracle for the wide query domain.
  std::minstd_rand random(201);
  constexpr std::uint32_t kNodes = 64;
  for (unsigned run = 0; run < 32; ++run) {
    definitions.clear();
    for (std::uint32_t i = 0; i < kNodes; ++i)
      definitions.push_back(
          {i == 0 || random() % 4 == 0
               ? EnvironmentRef{CapturedDepth{0}}
               : EnvironmentRef{EnvironmentId{
                     static_cast<std::uint32_t>(random() % i)}},
           {1, {}}});
    const auto forest =
        std::get<EnvironmentIndex>(EnvironmentIndex::Build(definitions));
    for (std::uint32_t i = 0; i < kNodes; ++i)
      for (const auto depth : {0U, 1U, 63U, 65535U, 65536U, UINT32_MAX}) {
        EnvironmentRef expected = EnvironmentId{i};
        auto remaining = depth;
        while (remaining) {
          if (auto* capture = std::get_if<CapturedDepth>(&expected)) {
            capture->value += remaining;
            break;
          }
          expected =
              definitions[std::get<EnvironmentId>(expected).value].parent;
          --remaining;
        }
        Check(std::get<LexicalBinding>(
                  forest.ResolveCapture(EnvironmentId{i}, {depth}, {0}))
                  .environment == expected);
      }
  }
  for (const std::uint32_t count : {200U, 201U, 4096U, 65538U}) {
    definitions.clear();
    for (std::uint32_t i = 0; i < count; ++i)
      definitions.push_back({i ? EnvironmentRef{EnvironmentId{i - 1}}
                               : EnvironmentRef{CapturedDepth{0}},
                             {1, {}}});
    const auto chain =
        std::get<EnvironmentIndex>(EnvironmentIndex::Build(definitions));
    for (std::uint32_t i = 0; i < count; ++i)
      Check(std::get<LexicalBinding>(
                chain.ResolveCapture(EnvironmentId{i}, {i}, {0}))
                .environment == EnvironmentRef{EnvironmentId{0}});
    if (count > UINT16_MAX) {
      const auto high = std::get<LexicalBinding>(
          chain.ResolveCapture(EnvironmentId{count - 1}, {65536}, {0}));
      Check(high.environment == EnvironmentRef{EnvironmentId{1}});
    }
  }
}
