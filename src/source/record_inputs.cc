#include "record_inputs.h"

namespace abcd::source {
std::optional<SourceFailure> VerifyRecordInputs(
    const analysis::SemanticMethod& method, const SourceAst& ast,
    std::size_t bytes) {
  const auto fail = [](abc::CodeOffset pc = {}) {
    return SourceFailure{SourceError::kInvalidEffect, pc};
  };
  if (ast.statements.size() != method.instructions.size()) return fail();
  // PC-order propagation is valid only in this forward normal CFG. Phis
  // receiving any entry-derived value are refused, never treated as copies.
  for (std::size_t i = 0; i < method.flow.blocks.size(); ++i) {
    const auto& block = method.flow.blocks[i];
    if (block.successor_count > block.successors.size()) return fail(block.begin);
    for (std::size_t edge = 0; edge < block.successor_count; ++edge)
      if (block.successors[edge].target.value <= i ||
          block.successors[edge].kind == analysis::FlowEdgeKind::kResume)
        return fail(block.begin);
  }
  const auto values = method.ssa.Values();
  if (values.size() > bytes / sizeof(std::uint8_t))
    return SourceFailure{SourceError::kResourceLimit, {}};
  std::vector<std::uint8_t> entry(values.size());
  if (entry.capacity() > bytes / sizeof(std::uint8_t))
    return SourceFailure{SourceError::kResourceLimit, {}};
  for (std::size_t i = 0; i < values.size(); ++i)
    entry[i] = std::holds_alternative<analysis::MethodEntry>(values[i].origin);
  for (std::size_t i = 0; i < method.instructions.size(); ++i) {
    const analysis::InstructionId id{static_cast<std::uint32_t>(i)};
    const auto reads = method.ssa.Reads(id);
    bool incoming = false;
    for (const auto& read : reads) incoming |= entry[read.value.value] != 0;
    if (!incoming) continue;
    const auto& instruction = method.instructions[i];
    const auto* copy = std::get_if<analysis::ScalarAssignment>(&instruction.operation);
    const auto* value = copy
        ? std::get_if<analysis::ScalarValue>(&copy->expression) : nullptr;
    const auto write = method.ssa.Write(id);
    if (!copy || copy->completion != analysis::CompletionEffect::kNormal ||
        !value || (!std::holds_alternative<analysis::Accumulator>(*value) &&
                   !std::holds_alternative<abc::RegisterId>(*value)) ||
        reads.size() != 1 || !write ||
        !std::holds_alternative<analysis::NoOperation>(ast.statements[i].operation))
      return fail(instruction.pc);
    entry[write->value] = 1;
  }
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (!std::holds_alternative<analysis::PhiValue>(values[i].origin)) continue;
    for (const auto& input : method.ssa.Inputs(
             analysis::SsaValueId{static_cast<std::uint32_t>(i)}))
      if (entry[input.value.value]) return fail();
  }
  return {};
}
}  // namespace abcd::source
