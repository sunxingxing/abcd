#ifndef ABCD_SOURCE_ARGUMENT_SEQUENCE_H_
#define ABCD_SOURCE_ARGUMENT_SEQUENCE_H_

#include <type_traits>

#include "abcd/source/semantic_source.h"

namespace abcd::source {
// These nodes emit expressions without declarations or control transfers.
// Own stores still require VerifyOwnFields; sequence syntax grants no new
// object provenance and never substitutes Set for an unproven DefineField.
inline bool IsSequenceStatement(const SourceStatement& statement) {
  return std::holds_alternative<SourceAssignment>(statement.operation) ||
         std::holds_alternative<SourceMemberStore>(statement.operation) ||
         std::holds_alternative<SourceOwnStore>(statement.operation) ||
         std::holds_alternative<analysis::NoOperation>(statement.operation);
}
inline bool HasArgumentValue(const SourceExpression& expression) {
  return std::visit([](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, analysis::ScalarExpression> ||
                  std::is_same_v<T, MemberExpression> ||
                  std::is_same_v<T, StringExpression> ||
                  std::is_same_v<T, StringComparison> ||
                  std::is_same_v<T, ModuleExpression> ||
                  std::is_same_v<T, GlobalExpression> ||
                  std::is_same_v<T, analysis::GlobalObjectLoad> ||
                  std::is_same_v<T, LocalExpression> ||
                  std::is_same_v<T, CapturedExpression> ||
                  std::is_same_v<T, FunctionExpression> ||
                  std::is_same_v<T, CallExpression> ||
                  std::is_same_v<T, MemberCallExpression> ||
                  std::is_same_v<T, analysis::ConstructorCall> ||
                  std::is_same_v<T, analysis::EmptyObjectCreation> ||
                  std::is_same_v<T, analysis::BufferedObjectCreation> ||
                  std::is_same_v<T, analysis::BufferedArrayCreation>) {
      // Effects remain in original order. These expressions cannot assign
      // caller-private VM bindings except through their explicit AST target.
      // Buffer writers still require primitive literals; calls are not pure.
      // Callable writers preserve names explicitly (named function syntax or
      // computed-property SetFunctionName), never by this assignment target.
      // Closure capture tokens/domain proofs stay at the original creation PC.
      return true;
    } else {
      // Class/home-object assembly is not covered by the callable-name audit.
      // Super, rest and await have separate owner
      // protocols. Do not admit them merely because they print expressions.
      static_assert(std::is_same_v<T, ClassExpression> ||
                    std::is_same_v<T, MethodExpression> ||
                    std::is_same_v<T, AccessorExpression> ||
                    std::is_same_v<T, analysis::SuperRange> ||
                    std::is_same_v<T, analysis::RestCopy> ||
                    std::is_same_v<T, analysis::AwaitValue>);
      return false;
    }
  }, expression);
}
struct ArgumentStep {
  abc::RegisterId target;
  std::size_t next;
};
inline std::optional<ArgumentStep> OrderedArgument(
    std::span<const SourceStatement> statements, std::size_t position,
    std::size_t end) {
  if (position >= end || end > statements.size()) return {};
  const auto& node = statements[position];
  if (const auto* assignment = std::get_if<SourceAssignment>(&node.operation)) {
    const auto* target = std::get_if<abc::RegisterId>(&assignment->target);
    if (!target || assignment->result == AssignmentResult::kUnused ||
        !HasArgumentValue(assignment->value)) return {};
    return ArgumentStep{*target, position + 1};
  }
  const auto* call = std::get_if<SourceStagedCall>(&node.operation);
  // The child's arguments must have no private commits: otherwise they could
  // overwrite an earlier outer argument before the original outer invocation.
  // Bottom-up certification avoids recursively scanning nested intervals.
  if (!call || call->arguments != CallArgumentLayout::kValues ||
      !call->stored_result || call->return_statement ||
      call->result == AssignmentResult::kUnused ||
      call->first_argument_statement.value != position + 1 ||
      call->call_statement.value <= position ||
      call->call_statement.value >= end ||
      call->stored_result->statement.value != call->call_statement.value + 1 ||
      call->stored_result->statement.value >= end) return {};
  // Leave the saved-result origin for the outer traversal to emit once.
  return ArgumentStep{call->stored_result->target,
                      std::size_t(call->call_statement.value) + 1};
}
// Keep unelided assignments, but distribute their values over native arguments.
// Strictly increasing targets prove distinct slots without pairwise searches.
// This is not dead-store elimination: later instructions may still read them.
inline bool HasOrderedArguments(std::span<const SourceStatement> statements,
                                std::size_t first, std::size_t end,
                                const analysis::ReceiverCall& call) {
  if (first > end || end > statements.size()) return false;
  std::uint32_t ordinal = 0;
  std::optional<abc::RegisterId> previous;
  for (auto i = first; i < end;) {
    if (std::holds_alternative<analysis::NoOperation>(statements[i].operation)) {
      ++i;
      continue;
    }
    const auto step = OrderedArgument(statements, i, end);
    const auto argument = call.At(ordinal);
    if (!step || !argument || step->target != *argument ||
        (previous && previous->value >= step->target.value)) return false;
    previous = step->target;
    i = step->next;
    ++ordinal;
  }
  // Zero arguments qualify only when every interval node was NoOperation;
  // any emitted assignment above fails call.At(0). Keep their PC anchors.
  return ordinal == call.Count();
}
}  // namespace abcd::source

#endif  // ABCD_SOURCE_ARGUMENT_SEQUENCE_H_
