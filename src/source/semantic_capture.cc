#include "semantic_capture.h"

#include <algorithm>
#include <tuple>

namespace abcd::source {
std::variant<analysis::LocalCell, analysis::CapturedSlot, SourceFailure>
ResolveCreationCapture(
    const analysis::SemanticMethod& method,
    const analysis::SemanticProof& proof, const FunctionExpression& function,
    analysis::CapturedSlot capture) {
  const auto instruction = function.instruction.value;
  const auto fail = [&] {
    return SourceFailure{SourceError::kCreation,
        instruction < method.instructions.size() ? method.instructions[instruction].pc
                                                : abc::CodeOffset{}};
  };
  if (instruction >= method.instructions.size() ||
      instruction >= proof.environments.before.size() ||
      !proof.environments.before[instruction]) return fail();
  const auto resolved = proof.environments.environments.ResolveCapture(
      *proof.environments.before[instruction], capture.depth, capture.slot);
  const auto* binding = std::get_if<analysis::LexicalBinding>(&resolved);
  if (!binding) return fail();
  if (const auto* depth = std::get_if<analysis::CapturedDepth>(&binding->environment))
    return analysis::CapturedSlot{*depth, binding->slot};
  const auto* environment = std::get_if<analysis::EnvironmentId>(&binding->environment);
  if (!environment || environment->value >= proof.locals.allocations.size()) return fail();
  const analysis::LocalCell cell{*environment, binding->slot};
  const auto key = [](auto item) { return std::tuple(item.environment.value, item.slot.value); };
  const auto& cells = proof.locals.cells;
  const auto found = std::lower_bound(cells.begin(), cells.end(), cell,
      [&](auto a, auto b) { return key(a) < key(b); });
  if (found == cells.end() || key(*found) != key(cell)) return fail();
  const auto id = static_cast<std::uint32_t>(found - cells.begin());
  const auto& stores = proof.locals.stores;
  auto store = std::lower_bound(stores.begin(), stores.end(), std::tuple(id, instruction),
      [](auto item, auto target) {
        return std::tuple(item.cell.value, item.instruction.value) < target;
      });
  if (store == stores.begin()) return fail();
  --store;
  if (store->cell.value != id) return fail();
  const auto& blocks = method.flow.blocks;
  auto block = std::upper_bound(blocks.begin(), blocks.end(), instruction,
      [](auto target, const auto& item) { return target < item.first_instruction; });
  if (block == blocks.begin()) return fail();
  --block;
  const auto allocation = proof.locals.allocations[environment->value];
  if (!allocation || allocation->value < block->first_instruction ||
      allocation->value >= store->instruction.value ||
      store->instruction.value < block->first_instruction ||
      (function.previous_creation &&
       function.previous_creation->value >= allocation->value)) return fail();
  return cell;
}
}  // namespace abcd::source
