#include "predicate_copies.h"

#include <type_traits>

#include "private_copy.h"

namespace abcd::source {
namespace {
bool HasMergedOrigin(const SourceStatement& statement) {
  return statement.fused_read || statement.fused_spill || statement.fused_reload ||
         statement.class_origin || statement.previous_origin;
}
std::size_t ReplaceSlot(PredicateInput& input, const analysis::ScalarSlot& target,
                            const analysis::ScalarValue& replacement) {
  std::size_t replaced = 0;
  const auto replace = [&](analysis::ScalarValue& value) {
    const auto* reg = std::get_if<abc::RegisterId>(&value);
    const auto* destination = std::get_if<abc::RegisterId>(&target);
    const bool matches = destination ? reg && *reg == *destination
        : std::holds_alternative<analysis::Accumulator>(value);
    if (matches) {
      value = replacement;
      ++replaced;
    }
  };
  std::visit([&](auto& predicate) {
    using T = std::decay_t<decltype(predicate)>;
    if constexpr (std::is_same_v<T, StringComparison>) {
      replace(predicate.left);
    } else {
      static_assert(std::is_same_v<T, analysis::ScalarExpression>);
      std::visit([&](auto& expression) {
        using E = std::decay_t<decltype(expression)>;
        if constexpr (std::is_same_v<E, analysis::ScalarValue>) replace(expression);
        else if constexpr (std::is_same_v<E, analysis::UnaryExpression>)
          replace(expression.input);
        else if constexpr (std::is_same_v<E, analysis::BinaryExpression>) {
          replace(expression.left);
          replace(expression.right);
        } else {
          // Property evaluation is outside this private predicate proof.
          static_assert(std::is_same_v<E, analysis::PropertyRead>);
        }
      }, predicate);
    }
  }, input);
  return replaced;
}
}  // namespace

void FoldPredicateCopies(const AstMethod& method, SourceAst& ast,
                         std::span<const std::size_t> uses) {
  for (const auto& block : method.flow.blocks) {
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    std::optional<std::size_t> previous;
    for (std::size_t i = block.first_instruction; i < end; ++i) {
      auto& tail = ast.statements[i];
      if (std::holds_alternative<analysis::NoOperation>(tail.operation) &&
          !HasMergedOrigin(tail) && IsPureCopyStep(method.instructions[i])) continue;
      if (previous && i == *previous + 1 &&
          IsRetargetedCopyAnchor(tail, ast.statements[*previous]) &&
          IsPureCopyStep(method.instructions[i])) continue;
      const auto producer = previous;
      previous = i;
      auto* branch = std::get_if<SourceConditionalBreak>(&tail.operation);
      auto* loop = std::get_if<SourceConditionalContinue>(&tail.operation);
      auto* input = branch ? &branch->input : loop ? &loop->input : nullptr;
      // FoldBranchExpressions owns the immediately preceding predicate PC.
      // Preserve its map relationship; do not infer provenance from JS text.
      if (!producer || !input || !*input || !tail.fused_read ||
          tail.read_distance != 1 || i == block.first_instruction ||
          ast.statements[i - 1].pc != *tail.fused_read ||
          !std::holds_alternative<analysis::NoOperation>(ast.statements[i - 1].operation) ||
          tail.fused_spill || tail.fused_reload || tail.class_origin ||
          tail.previous_origin || !IsPureCopyStep(method.instructions[i - 1])) continue;
      auto& head = ast.statements[*producer];
      const auto* assignment = std::get_if<SourceAssignment>(&head.operation);
      const auto* scalar = assignment
          ? std::get_if<analysis::ScalarExpression>(&assignment->value) : nullptr;
      const auto* value = scalar ? std::get_if<analysis::ScalarValue>(scalar) : nullptr;
      if (!assignment || !value || HasMergedOrigin(head) ||
          assignment->result != AssignmentResult::kStored || assignment->object_field ||
          std::holds_alternative<analysis::EmptyArray>(*value)) continue;
      const auto* target = std::get_if<abc::RegisterId>(&assignment->target);
      std::optional<analysis::SsaValueId> output;
      if (target) {
        output = PrivateCopyOutput(method, ast, *producer, i - 1, *target, uses);
      } else {
        // A physical register reload, not arbitrary expression substitution.
        // No later pass may rely on this acc commit: its sole live use is the
        // predicate that will consume the register directly below.
        const auto& instruction = method.instructions[*producer];
        const auto* copy = std::get_if<analysis::ScalarAssignment>(&instruction.operation);
        const auto* input = copy
            ? std::get_if<analysis::ScalarValue>(&copy->expression) : nullptr;
        const auto* original = input ? std::get_if<abc::RegisterId>(input) : nullptr;
        const auto* reg = std::get_if<abc::RegisterId>(value);
        if (!copy || !original || !reg || *original != *reg ||
            !std::holds_alternative<analysis::Accumulator>(copy->destination) ||
            copy->completion != analysis::CompletionEffect::kNormal ||
            !IsPureCopyStep(instruction)) continue;
        output = method.ssa.Write({static_cast<std::uint32_t>(*producer)});
      }
      if (!output || output->value >= uses.size() || uses[output->value] != 1) continue;
      bool consumed = false;
      for (const auto& read : method.ssa.Reads({static_cast<std::uint32_t>(i - 1)}))
        consumed |= read.value == *output;
      if (!consumed) continue;
      auto rewritten = **input;
      if (ReplaceSlot(rewritten, assignment->target, *value) != 1) continue;
      // Private reads/primitive constants are effect-free. Every intervening
      // source node is empty, and SSA excludes normal/exceptional observers
      // of the removed commit. No predicate effect or branch is relocated.
      **input = std::move(rewritten);
      head.operation = analysis::NoOperation{};
      if (!target) {
        tail.fused_reload = head.pc;
        tail.reload_distance = i - *producer;
      }
    }
  }
}
}  // namespace abcd::source
