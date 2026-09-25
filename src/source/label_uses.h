#ifndef ABCD_SOURCE_LABEL_USES_H_
#define ABCD_SOURCE_LABEL_USES_H_

#include <span>
#include <type_traits>

#include "abcd/source/semantic_source.h"

namespace abcd::source {
enum class LabelUse : std::uint8_t { kAbsent, kReferenced };

// Match native if emission, not raw CFG edges: its header branch and each
// arm's final jump are consumed. Staged arguments have a different printer
// and must not use this index. Extra references are safe; missing ones aren't.
inline void CollectLabelUses(const SourceAst& ast,
                             const analysis::NormalFlow& flow,
                             std::span<LabelUse> uses) {
  if (ast.argument_region)
    throw SourceFailure{SourceError::kInvalidEffect, {}};
  const auto mark = [&](analysis::BlockId target) {
    if (target.value >= uses.size())
      throw SourceFailure{SourceError::kInvalidEffect, {}};
    uses[target.value] = LabelUse::kReferenced;
  };
  const auto block = [&](analysis::BlockId id, bool omit_last,
                         bool fallthrough) {
    if (id.value >= flow.blocks.size() || id.value >= ast.fallthrough.size())
      throw SourceFailure{SourceError::kInvalidEffect, {}};
    const auto& value = flow.blocks[id.value];
    auto end = std::size_t(value.first_instruction) + value.instruction_count;
    if (end > ast.statements.size() || (omit_last && !value.instruction_count))
      throw SourceFailure{SourceError::kInvalidEffect, value.begin};
    if (omit_last) --end;
    for (auto i = std::size_t(value.first_instruction); i < end; ++i) {
      const auto& operation = ast.statements[i].operation;
      if (const auto* jump = std::get_if<SourceBreak>(&operation))
        mark(jump->target);
      else if (const auto* branch = std::get_if<SourceConditionalBreak>(&operation))
        mark(branch->target);
    }
    if (fallthrough && ast.fallthrough[id.value]) mark(*ast.fallthrough[id.value]);
  };
  const auto arm = [&](analysis::BlockId id) {
    if (id.value >= flow.blocks.size())
      throw SourceFailure{SourceError::kInvalidEffect, {}};
    block(id, flow.blocks[id.value].transfer == abc::ControlTransfer::kJump,
          false);
  };
  const auto visit = [&](const auto& self, const analysis::RegionNode& node,
                         std::size_t depth) -> void {
    if (depth > analysis::kMaxSemanticRegionDepth)
      throw SourceFailure{SourceError::kResourceLimit, {}};
    std::visit([&](const auto& region) {
      using T = std::decay_t<decltype(region)>;
      if constexpr (std::is_same_v<T, analysis::RegionBlock>) {
        block(region.id, false, true);
      } else if constexpr (std::is_same_v<T, analysis::RegionIf>) {
        block(region.header, true, false);
        arm(region.body);
        if (region.alternate) arm(*region.alternate);
      } else if constexpr (std::is_same_v<T, analysis::RegionSequence>) {
        for (const auto& child : region.children) self(self, child, depth + 1);
      } else if constexpr (std::is_same_v<T, analysis::RegionTry>) {
        if (!region.body || !region.handler)
          throw SourceFailure{SourceError::kInvalidEffect, {}};
        self(self, *region.body, depth + 1);
        self(self, *region.handler, depth + 1);
      } else if constexpr (std::is_same_v<T, analysis::RegionLabel> ||
                           std::is_same_v<T, analysis::RegionLoop>) {
        if (!region.body)
          throw SourceFailure{SourceError::kInvalidEffect, {}};
        self(self, *region.body, depth + 1);
      } else {
        static_assert(std::is_same_v<T, void>, "unhandled source region");
      }
    }, node.value);
  };
  visit(visit, ast.regions.root, 0);
}
}  // namespace abcd::source
#endif
