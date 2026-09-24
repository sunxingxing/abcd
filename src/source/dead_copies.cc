#include "dead_copies.h"

namespace abcd::source {
namespace {
bool SlotCopy(const analysis::SemanticOperation& operation) {
  const auto* copy = std::get_if<analysis::ScalarAssignment>(&operation);
  if (!copy || copy->completion != analysis::CompletionEffect::kNormal) return false;
  const auto* value = std::get_if<analysis::ScalarValue>(&copy->expression);
  return value && (std::holds_alternative<analysis::Accumulator>(*value) ||
                   std::holds_alternative<abc::RegisterId>(*value));
}
}
std::optional<SourceFailure> ElideDeadCopies(const AstMethod& method,
    SourceAst& ast, std::span<std::size_t> uses, std::size_t bytes) {
  if (method.instructions.size() > bytes / sizeof(analysis::InstructionId))
    return SourceFailure{SourceError::kResourceLimit, {}};
  std::vector<analysis::InstructionId> pending;
  pending.reserve(method.instructions.size());
  if (pending.capacity() > bytes / sizeof(analysis::InstructionId))
    return SourceFailure{SourceError::kResourceLimit, {}};
  const auto enqueue = [&](analysis::SsaValueId value) {
    if (uses[value.value]) return;
    const auto origin = method.ssa.Definition(value);
    if (origin && SlotCopy(method.instructions[origin->value].operation) &&
        std::holds_alternative<SourceAssignment>(ast.statements[origin->value].operation))
      pending.push_back(*origin);
  };
  for (std::size_t i = 0; i < uses.size(); ++i)
    enqueue(analysis::SsaValueId{static_cast<std::uint32_t>(i)});
  // Sparse DCE restricted to slot copies. Each definition enters once when
  // its exact count reaches zero, including chains visited out of PC order.
  // Never decrement uses for fused nodes: their replacement still consumes
  // the original values, potentially at a different generated position.
  for (std::size_t head = 0; head < pending.size(); ++head) {
    const auto instruction = pending[head];
    const auto reads = method.ssa.Reads(instruction);
    if (reads.size() != 1 || reads.front().value.value >= uses.size() ||
        !uses[reads.front().value.value])
      return SourceFailure{SourceError::kInvalidEffect, ast.statements[instruction.value].pc};
    ast.statements[instruction.value].operation = analysis::NoOperation{};
    --uses[reads.front().value.value];
    enqueue(reads.front().value);
  }
  return {};
}
}  // namespace abcd::source
