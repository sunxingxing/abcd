#ifndef ABCD_TESTS_INSTRUCTION_POSITIONS_H_
#define ABCD_TESTS_INSTRUCTION_POSITIONS_H_

#include <cstdlib>
#include <type_traits>

#include "../../src/source/instruction_sources.h"
#include "../../src/source/discarded_read.h"
#include "../../src/source/nullish_syntax.h"
#include "../../src/source/tail_completion.h"
#include "../../src/source/binding_initializer.h"
#include "../../src/source/loop_syntax.h"
#include "../../src/source/expression_children.h"
#include "abcd/source/tree_printer.h"

namespace abcd::test {
// Test oracle for an already checked source census. Uses printer event IDs,
// never searches generated text for an instruction's spelling.
inline void CheckInstructionPositions(const source::FunctionTree& tree,
    const source::InstructionSources& sources, const source::PrintedFunction& positions,
    source::GeneratedRange owner) {
  using namespace source;
  const auto check = [](bool ok) { if (!ok) std::abort(); };
  const auto nullish_result = PlanNullishSyntax(tree);
  check(std::holds_alternative<NullishSyntax>(nullish_result));
  const auto& nullish = std::get<NullishSyntax>(nullish_result);
  // Independently recompute the recovery plan in this test oracle. Production
  // publication consumes emitted extents instead of running recovery again.
  const auto loops = std::get<LoopSyntax>(PlanLoopSyntax(tree));
  const auto bindings = std::get<BindingInitializers>(PlanBindingInitializers(tree, loops, nullish));
  std::vector<bool> omitted_expressions(tree.expressions.size());
  std::vector<ExprId> pending;
  for (std::size_t i=0;i<tree.statements.size();++i) if (bindings.omitted[i]) {
    if (const auto* effect=std::get_if<ExpressionStmt>(&tree.statements[i].statement)) {
      if (bindings.forwarded_statements[i]) omitted_expressions[effect->expression.value]=true;
      else pending.push_back(effect->expression);
    }
  }
  while (!pending.empty()) {
    const auto id=pending.back();pending.pop_back();
    if (omitted_expressions[id.value]) continue;
    omitted_expressions[id.value]=true;
    VisitExpressionChildren(tree,tree.expressions[id.value].expression,[&](ExprId child){pending.push_back(child);});
    if (const auto replacement=bindings.replacements[id.value]) pending.push_back(*replacement);
  }
  const auto completion = positions.fragment == FunctionFragment::kStaticFields ||
      positions.fragment == FunctionFragment::kImplicitConstructor ? std::nullopt : ImplicitTailCompletion(tree);
  std::vector<std::optional<StmtId>> discarded(tree.expressions.size());
  for (std::size_t i = 0; i < tree.statements.size(); ++i) {
    const StmtId statement{static_cast<std::uint32_t>(i)};
    if (const auto read = DiscardedInvocationRead(tree, statement)) discarded[read->value] = statement;
  }
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
          GeneratedExtent extent=GeneratedExtent::kSpan;
          if constexpr(std::is_same_v<T,ExprId>) extent=positions.expression_extents[target.value];
          else if constexpr(std::is_same_v<T,StmtId>) extent=positions.statement_extents[target.value];
          else if constexpr(std::is_same_v<T,PropertyId>) extent=positions.property_extents[target.value];
          check((extent==GeneratedExtent::kBoundary)==(range.begin==range.end));
          bool binding_omission=false;
          if constexpr(std::is_same_v<T,StmtId>) binding_omission=bindings.omitted[target.value];
          else if constexpr(std::is_same_v<T,ExprId>) binding_omission=omitted_expressions[target.value];
          if (binding_omission) {
            check(extent==GeneratedExtent::kBoundary && owner.begin<=range.begin && range.end<=owner.end);
            return;
          }

          std::optional<StmtId> omitted_read;
          bool tail = false;
          if constexpr (std::is_same_v<T, ExprId>) {
            omitted_read = discarded[target.value];
            tail = completion && completion->value == target;
          } else if constexpr (std::is_same_v<T, StmtId>) {
            if (DiscardedInvocationRead(tree, target)) omitted_read = target;
            tail = completion && completion->statement == target;
          }
          if (omitted_read) {
            // Omitted native invocation reads retain both original event IDs
            // at one owned boundary; arbitrary empty expression ranges fail.
            const auto read = *DiscardedInvocationRead(tree, *omitted_read);
            check(owner.begin <= range.begin && range.begin == range.end && range.end <= owner.end &&
                range == positions.statements[omitted_read->value] && range == positions.expressions[read.value]);
            return;
          }
          if (tail) {
            check(range.begin == range.end && range == positions.statements[completion->statement.value] &&
                (!completion->value || range == positions.expressions[completion->value->value]));
            if (positions.fragment == FunctionFragment::kModuleBody) check(range.end == owner.end);
            else {
              const auto block = positions.statements[tree.body.value];
              check(owner.begin <= block.begin && block.begin <= range.begin &&
                  range.end < block.end && block.end <= owner.end);
              const auto& body = std::get<BlockStmt>(tree.statements[tree.body.value].statement);
              if (body.child_count > 1)
                check(positions.statements[tree.children[body.first_child + body.child_count - 2].value].end <= range.begin);
            }
            return;
          }
          if constexpr (std::is_same_v<T, ExprId> || std::is_same_v<T, StmtId> || std::is_same_v<T, PropertyId>) {
            if (const auto omitted = nullish.Omission(target)) {
              const auto root = *omitted;
              const auto base = nullish.Base(tree, root);
              const auto guard = std::visit([&](auto id) {
                if constexpr(std::is_same_v<decltype(id),ExprId>) return positions.expressions[id.value];
                else return positions.statements[id.value];
              },root);
              const auto receiver = positions.expressions[base.value];
              const auto& projection=nullish.Projection(root);
              auto access = guard;
              if (const auto* member = std::get_if<OptionalMemberSyntax>(&projection))
                access = positions.expressions[member->member.value];
              else if (const auto* call = std::get_if<OptionalCallSyntax>(&projection))
                access = positions.expressions[call->call.value];
              else {
                const auto& coalesce = std::get<NullishFallbackSyntax>(projection);
                const auto fallback = positions.expressions[coalesce.fallback.value];
                check(receiver.end <= fallback.begin && fallback.begin < fallback.end && fallback.end <= guard.end);
              }
              check(range == GeneratedRange{receiver.end, receiver.end} &&
                  owner.begin <= guard.begin && guard.begin <= access.begin &&
                  access.begin <= receiver.begin && receiver.begin < receiver.end &&
                  receiver.end < access.end && access.end <= guard.end && guard.end <= owner.end);
              return;
            }
          }
          check(owner.begin <= range.begin && range.begin < range.end && range.end <= owner.end);
        }
      }
    }, site.target);
  }
}
}  // namespace abcd::test
#endif
