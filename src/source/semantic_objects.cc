#include "semantic_objects.h"

namespace abcd::source {
PrivateObjects::Origin PrivateObjects::Find(
    std::optional<analysis::SsaValueId> value, std::size_t block_begin) const {
  if (!value) return {};
  const auto origin = origins_[value->value];
  if (!origin || origin->value < block_begin ||
      (publication_ && origin->value <= publication_->value)) return {};
  return origin;
}

void PrivateObjects::Observe(const analysis::SemanticMethod& method,
                             analysis::InstructionId instruction,
                             std::size_t block_begin,
                             bool verified_own_store) {
  const auto& operation = method.instructions[instruction.value].operation;
  const auto reads = method.ssa.Reads(instruction);
  Origin origin;
  bool copy = false;
  if (const auto* assignment =
          std::get_if<analysis::ScalarAssignment>(&operation)) {
    if (assignment->completion == analysis::CompletionEffect::kNormal) {
      if (const auto* value =
              std::get_if<analysis::ScalarValue>(&assignment->expression)) {
        copy = std::holds_alternative<analysis::Accumulator>(*value) ||
               std::holds_alternative<abc::RegisterId>(*value);
      }
    }
  }
  if (copy && reads.size() == 1) {
    origin = Find(reads.front().value, block_begin);
  } else if (!verified_own_store) {
    // Getters/calls may execute user code but cannot access private VM slots.
    // Any non-copy use of a tracked reference may expose its entire connected
    // object graph. Invalidate the epoch, not just the directly read receiver.
    // Lexical/module/property stores are covered by their SSA value reads;
    // subsequent closure creation cannot recover an unpublished register.
    for (const auto& read : reads) {
      if (Find(read.value, block_begin)) {
        publication_ = instruction;
        break;
      }
    }
  }
  if (std::holds_alternative<analysis::BufferedObjectCreation>(operation))
    origin = instruction;
  if (const auto write = method.ssa.Write(instruction))
    origins_[write->value] = origin;
}
}  // namespace abcd::source
