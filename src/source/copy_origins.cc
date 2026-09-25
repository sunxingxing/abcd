#include "copy_origins.h"

#include "semantic_ast_input.h"

namespace abcd::source {
CopyOrigins::CopyOrigins(std::size_t values) : origins_(values) {
  for (std::size_t i = 0; i < values; ++i)
    origins_[i] = analysis::SsaValueId{static_cast<std::uint32_t>(i)};
}
analysis::SsaValueId CopyOrigins::At(analysis::SsaValueId value) const {
  return origins_[value.value];
}
std::size_t CopyOrigins::AllocatedBytes() const {
  return origins_.capacity() * sizeof(analysis::SsaValueId);
}
void CopyOrigins::Observe(const AstMethod& method,
                          analysis::InstructionId instruction) {
  const auto* copy = std::get_if<analysis::ScalarAssignment>(
      &method.instructions[instruction.value].operation);
  if (!copy || copy->completion != analysis::CompletionEffect::kNormal) return;
  const auto* value = std::get_if<analysis::ScalarValue>(&copy->expression);
  if (!value || (!std::holds_alternative<analysis::Accumulator>(*value) &&
                 !std::holds_alternative<abc::RegisterId>(*value))) return;
  const auto reads = method.ssa.Reads(instruction);
  const auto write = method.ssa.Write(instruction);
  if (reads.size() == 1 && write)
    origins_[write->value] = At(reads.front().value);
}
}  // namespace abcd::source
