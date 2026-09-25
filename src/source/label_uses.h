#ifndef ABCD_SOURCE_LABEL_USES_H_
#define ABCD_SOURCE_LABEL_USES_H_

#include <span>
#include <type_traits>

#include "abcd/source/semantic_source.h"
#include "argument_region.h"

namespace abcd::source {
enum class LabelUse : std::uint8_t { kAbsent, kReferenced };

// Match native if emission, not raw CFG edges: its header branch and each
// arm's final jump are consumed. Argument-interior jumps belong to the IIFE,
// not outer labels. Extra references are safe; missing ones aren't.
inline void CollectLabelUses(const SourceAst& ast,
                             const analysis::NormalFlow& flow,
                             std::span<LabelUse> uses) {
  const auto mark = [&](analysis::BlockId target) {
    if (target.value >= uses.size())
      throw SourceFailure{SourceError::kInvalidEffect, {}};
    uses[target.value] = LabelUse::kReferenced;
  };
  const auto block = [&](analysis::BlockId id, bool omit_last,
                         bool fallthrough,
                         std::optional<analysis::BlockId> continuation = {}) {
    if (id.value >= flow.blocks.size() || id.value >= ast.fallthrough.size())
      throw SourceFailure{SourceError::kInvalidEffect, {}};
    const auto& value = flow.blocks[id.value];
    auto end = std::size_t(value.first_instruction) + value.instruction_count;
    if (end > ast.statements.size() || (omit_last && !value.instruction_count))
      throw SourceFailure{SourceError::kInvalidEffect, value.begin};
    if (omit_last) --end;
    for (auto i = std::size_t(value.first_instruction); i < end; ++i) {
      if (IsArgumentInterior(ast, i)) continue;
      const auto& operation = ast.statements[i].operation;
      if (const auto* jump = std::get_if<SourceBreak>(&operation)) {
        if (i + 1 != end || !continuation || jump->target != *continuation)
          mark(jump->target);
      }
      else if (const auto* branch = std::get_if<SourceConditionalBreak>(&operation))
        mark(branch->target);
    }
    if (fallthrough && !IsArgumentInterior(ast, end) && ast.fallthrough[id.value] &&
        ast.fallthrough[id.value] != continuation)
      mark(*ast.fallthrough[id.value]);
  };
  const auto arm = [&](analysis::BlockId id) {
    if (id.value >= flow.blocks.size())
      throw SourceFailure{SourceError::kInvalidEffect, {}};
    block(id, flow.blocks[id.value].transfer == abc::ControlTransfer::kJump,
          false);
  };
  const auto staged = [&](analysis::BlockId id) {
    if (id.value >= flow.blocks.size())
      throw SourceFailure{SourceError::kInvalidEffect, {}};
    const auto& value = flow.blocks[id.value];
    return HasArgumentOverlap(ast, value.first_instruction,
        std::size_t(value.first_instruction) + value.instruction_count);
  };
  const auto visit = [&](const auto& self, const analysis::RegionNode& node,
                         std::size_t depth,
                         std::optional<analysis::BlockId> continuation = {}) -> void {
    if (depth > analysis::kMaxSemanticRegionDepth)
      throw SourceFailure{SourceError::kResourceLimit, {}};
    std::visit([&](const auto& region) {
      using T = std::decay_t<decltype(region)>;
      if constexpr (std::is_same_v<T, analysis::RegionBlock>) {
        block(region.id, false, true, continuation);
      } else if constexpr (std::is_same_v<T, analysis::RegionIf>) {
        // The printer falls back to RegionBlock emission if an argument
        // interval intersects this if. Retain every outer jump in that mode.
        if (staged(region.header) || staged(region.body) ||
            (region.alternate && staged(*region.alternate))) {
          block(region.header, false, true);
          block(region.body, false, true);
          if (region.alternate) block(*region.alternate, false, true);
        } else {
          block(region.header, true, false);
          arm(region.body);
          if (region.alternate) arm(*region.alternate);
        }
      } else if constexpr (std::is_same_v<T, analysis::RegionBranch>) {
        if (!region.body || !region.alternate)
          throw SourceFailure{SourceError::kInvalidEffect, {}};
        // Cross-block argument owners retain their independent label protocol.
        block(region.header, !ast.argument_region, ast.argument_region.has_value());
        const auto next = ast.argument_region ? std::optional<analysis::BlockId>{} : region.join;
        self(self, *region.body, depth + 1, next);
        self(self, *region.alternate, depth + 1, next);
      } else if constexpr (std::is_same_v<T, analysis::RegionSequence>) {
        for (std::size_t i = 0; i < region.children.size(); ++i)
          self(self, region.children[i], depth + 1,
               i + 1 == region.children.size() ? continuation : std::nullopt);
      } else if constexpr (std::is_same_v<T, analysis::RegionTry>) {
        if (!region.body || !region.handler)
          throw SourceFailure{SourceError::kInvalidEffect, {}};
        self(self, *region.body, depth + 1);
        self(self, *region.handler, depth + 1);
      } else if constexpr (std::is_same_v<T, analysis::RegionLabel> ||
                           std::is_same_v<T, analysis::RegionLoop>) {
        if (!region.body)
          throw SourceFailure{SourceError::kInvalidEffect, {}};
        self(self, *region.body, depth + 1,
             std::is_same_v<T, analysis::RegionLabel> ? continuation : std::nullopt);
      } else {
        static_assert(std::is_same_v<T, void>, "unhandled source region");
      }
    }, node.value);
  };
  visit(visit, ast.regions.root, 0);
}
}  // namespace abcd::source
#endif
