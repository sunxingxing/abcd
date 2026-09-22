#include "lexical_value_index.h"

#include <algorithm>

#include "idf_scratch.h"

namespace abcd::analysis {
void RegisterValueFlow::LexicalIndex::Charge(std::uint64_t units) {
  if (!quota.Take(units)) throw detail::IdfWorkLimit{};
}
bool RegisterValueFlow::LexicalIndex::Less(const CellKey& a, const CellKey& b) {
  return a.environment != b.environment ? a.environment < b.environment
                                        : a.slot.value < b.slot.value;
}
void RegisterValueFlow::LexicalIndex::Collect(
    const LexicalValueContext& context) {
  const auto& flow = context.environments;
  const auto fail = [](std::optional<InstructionId> instruction = {}) {
    return ValueFlowFailure{ValueFlowError::kInvalidLexicals, instruction, {}};
  };
  if (flow.before.size() != body.instructions.size() ||
      flow.allocations.size() != flow.environments.Size() ||
      !body.tries.empty() || !body.handlers.empty())
    throw fail();
  std::optional<InstructionId> previous;
  for (const auto& site : flow.bindings) {
    Charge();
    const InstructionId instruction{site.instruction};
    if ((previous && *previous >= instruction) ||
        instruction.value >= body.instructions.size() ||
        !flow.before[instruction.value])
      throw fail(instruction);
    previous = instruction;
    const auto operation = LiftLexicalInstruction(
        body.instructions[instruction.value].decoded, frame_size);
    const LexicalAddress* address = nullptr;
    if (const auto* load = std::get_if<LexicalLoad>(&operation))
      address = &load->address;
    if (const auto* store = std::get_if<LexicalStore>(&operation))
      address = &store->address;
    if (!address) throw fail(instruction);
    const auto resolved =
        flow.environments.Resolve(*flow.before[instruction.value], *address);
    const auto* binding = std::get_if<LexicalBinding>(&resolved);
    if (!binding || binding->environment != site.binding.environment ||
        binding->slot.value != site.binding.slot.value)
      throw fail(instruction);
    const auto* owner = std::get_if<EnvironmentId>(&binding->environment);
    if (!owner) continue;
    const auto escaped = context.escape.MayEscape(*owner);
    if (!escaped) throw fail(instruction);
    if (!*escaped) cells.push_back({*owner, binding->slot});
  }
  std::sort(cells.begin(), cells.end(), [&](const auto& a, const auto& b) {
    Charge();
    return Less(a, b);
  });
  cells.erase(std::unique(cells.begin(), cells.end(),
                          [&](const auto& a, const auto& b) {
                            Charge();
                            return !Less(a, b) && !Less(b, a);
                          }),
              cells.end());
  for (const auto& site : flow.bindings) {
    Charge();
    const auto* owner = std::get_if<EnvironmentId>(&site.binding.environment);
    if (!owner || *context.escape.MayEscape(*owner)) continue;
    const CellKey key{*owner, site.binding.slot};
    const auto found = std::lower_bound(cells.begin(), cells.end(), key,
                                        [&](const auto& a, const auto& b) {
                                          Charge();
                                          return Less(a, b);
                                        });
    if (found == cells.end() || Less(key, *found))
      throw fail(InstructionId{site.instruction});
    const CellId cell{static_cast<std::size_t>(found - cells.begin())};
    const InstructionId instruction{site.instruction};
    const auto operation = LiftLexicalInstruction(
        body.instructions[instruction.value].decoded, frame_size);
    if (std::holds_alternative<LexicalLoad>(operation)) {
      const auto value = ssa.Write(instruction);
      if (!value || value->value >= ssa.Values().size())
        throw fail(instruction);
      accesses.push_back({cell, *value, AccessKind::kLoad});
    } else {
      // Modern stores read acc; deprecated stores read their explicit register.
      // Register SSA already preserves this distinction and read-before-write.
      const auto reads = ssa.Reads(instruction);
      if (reads.size() != 1 || reads.front().value.value >= ssa.Values().size())
        throw fail(instruction);
      accesses.push_back({cell, reads.front().value, AccessKind::kStore});
    }
  }
}
}  // namespace abcd::analysis
