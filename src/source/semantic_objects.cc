#include "semantic_objects.h"

#include <utility>

namespace abcd::source {
std::size_t PrivateObjects::Root(std::size_t id) const {
  while (allocations_[id].parent != id) id = allocations_[id].parent;
  return id;
}
void PrivateObjects::Connect(analysis::InstructionId left,
                             analysis::InstructionId right) {
  auto a = Root(left.value);
  auto b = Root(right.value);
  if (a == b) return;
  if (allocations_[a].rank < allocations_[b].rank) std::swap(a, b);
  allocations_[b].parent = a;
  allocations_[a].escaped |= allocations_[b].escaped;
  if (allocations_[a].rank == allocations_[b].rank) ++allocations_[a].rank;
}
PrivateObjects::Origin PrivateObjects::Identity(
    analysis::SsaValueId value, std::size_t block_begin) const {
  const auto origin = origins_[value.value];
  return origin && origin->value >= block_begin ? origin : Origin{};
}
PrivateObjects::Origin PrivateObjects::Find(
    std::optional<analysis::SsaValueId> value, std::size_t block_begin) const {
  if (!value) return {};
  const auto origin = Identity(*value, block_begin);
  if (!origin || allocations_[Root(origin->value)].escaped) return {};
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
    // Keep aliases even after publication so later containment cannot make an
    // escaped component appear private again.
    origin = Identity(reads.front().value, block_begin);
  } else if (verified_own_store) {
    Origin first;
    for (const auto& read : reads) {
      if (const auto item = Identity(read.value, block_begin)) {
        if (first) Connect(*first, *item);
        else first = item;
      }
    }
  } else {
    // Getters/calls may execute user code but cannot access private VM slots.
    // Any non-copy use of a tracked reference may expose its entire connected
    // object graph. Union-by-rank tracks conservative bidirectional containment
    // in O(log I) per reference without rescanning every older allocation.
    // Lexical/module/property stores are covered by their SSA value reads;
    // subsequent closure creation cannot recover an unpublished register.
    for (const auto& read : reads) {
      if (const auto item = Identity(read.value, block_begin))
        allocations_[Root(item->value)].escaped = true;
    }
  }
  if (std::holds_alternative<analysis::BufferedObjectCreation>(operation))
    origin = instruction;
  if (const auto write = method.ssa.Write(instruction))
    origins_[write->value] = origin;
}
}  // namespace abcd::source
