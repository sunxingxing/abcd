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
// Keep all assignments, but distribute their values over native arguments.
// Strictly increasing targets prove distinct slots without pairwise searches.
// This is not dead-store elimination: later instructions may still read them.
inline bool HasOrderedArguments(std::span<const SourceStatement> statements,
                                const analysis::ReceiverCall& call) {
  std::uint32_t ordinal = 0;
  std::optional<abc::RegisterId> previous;
  for (const auto& statement : statements) {
    if (std::holds_alternative<analysis::NoOperation>(statement.operation)) continue;
    const auto* assignment = std::get_if<SourceAssignment>(&statement.operation);
    const auto* target = assignment
        ? std::get_if<abc::RegisterId>(&assignment->target) : nullptr;
    const auto argument = call.At(ordinal);
    if (!target || !argument || *target != *argument ||
        (previous && previous->value >= target->value)) return false;
    if (!HasArgumentValue(assignment->value)) return false;
    previous = *target;
    ++ordinal;
  }
  return ordinal != 0 && ordinal == call.Count();
}
}  // namespace abcd::source

#endif  // ABCD_SOURCE_ARGUMENT_SEQUENCE_H_
