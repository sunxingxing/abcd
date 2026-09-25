#ifndef ABCD_SOURCE_ARGUMENT_SEQUENCE_H_
#define ABCD_SOURCE_ARGUMENT_SEQUENCE_H_

#include "abcd/source/semantic_source.h"

namespace abcd::source {
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
    // These expressions only write the explicit target. Hidden creation and
    // capture consumers need their own audit before participating here.
    if (!std::holds_alternative<analysis::ScalarExpression>(assignment->value) &&
        !std::holds_alternative<MemberExpression>(assignment->value) &&
        !std::holds_alternative<StringExpression>(assignment->value)) return false;
    previous = *target;
    ++ordinal;
  }
  return ordinal != 0 && ordinal == call.Count();
}
}  // namespace abcd::source

#endif  // ABCD_SOURCE_ARGUMENT_SEQUENCE_H_
