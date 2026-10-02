#ifndef ABCD_TESTS_INSTRUCTION_POSITIONS_H_
#define ABCD_TESTS_INSTRUCTION_POSITIONS_H_

#include <cstdlib>
#include <type_traits>

#include "../../src/source/instruction_sources.h"
#include "abcd/source/tree_printer.h"

namespace abcd::test {
// Test oracle for an already checked source census. Uses printer event IDs,
// never searches generated text for an instruction's spelling.
inline void CheckInstructionPositions(const source::FunctionTree& tree,
    const source::InstructionSources& sources, const source::PrintedFunction& positions,
    source::GeneratedRange owner) {
  using namespace source;
  const auto check = [](bool ok) { if (!ok) std::abort(); };
  check(positions.identity == tree.identity && positions.method == tree.method &&
      positions.value_revision == tree.value_revision && positions.cfg_revision == tree.cfg_revision &&
      owner.begin < owner.end && positions.bytes == owner.end - owner.begin);
  for (const auto& site : sources.occurrences) {
    check(site.site.value < sources.input_sites);
    check(site.origin.value < tree.origins.size());
    std::visit([&](auto target) {
      using T = decltype(target);
      if constexpr (std::is_same_v<T, ElidedInstruction>) {
        const auto* origin = std::get_if<ElidedOrigin>(&tree.origins[site.origin.value]);
        check(origin && origin->site == site.site);
      } else if constexpr (std::is_same_v<T, UnreachableInstruction> || std::is_same_v<T, CleanupInstruction>) {
        const auto* origin = std::get_if<OriginalSite>(&tree.origins[site.origin.value]);
        check(origin && origin->site == site.site);
      } else {
        const auto* origin = std::get_if<OriginalSite>(&tree.origins[site.origin.value]);
        check(origin && origin->site == site.site);
        if constexpr (std::is_same_v<T, AnchorId>) {
          check(target.value < positions.anchors.size());
          check(owner.begin <= positions.anchors[target.value] && positions.anchors[target.value] <= owner.end);
        } else {
          const auto& ranges = [&]() -> const std::vector<GeneratedRange>& {
            if constexpr (std::is_same_v<T, ExprId>) return positions.expressions;
            else if constexpr (std::is_same_v<T, StmtId>) return positions.statements;
            else if constexpr (std::is_same_v<T, PropertyId>) return positions.properties;
            else { static_assert(std::is_same_v<T, ParameterId>); return positions.parameters; }
          }();
          check(target.value < ranges.size());
          const auto range = ranges[target.value];
          check(owner.begin <= range.begin && range.begin < range.end && range.end <= owner.end);
        }
      }
    }, site.target);
  }
}
}  // namespace abcd::test
#endif
