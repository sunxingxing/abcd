#include "module_read_snapshot.h"

#include <new>
#include <stdexcept>
#include "abcd/source/module_exports.h"

namespace abcd::source {
std::variant<ModuleReadSnapshot, SourceFailure> SnapshotModuleReads(
    std::span<const analysis::SemanticInstruction> instructions,
    const MethodConstants& constants, analysis::SemanticBudget budget) {
  const auto limit = [] { return SourceFailure{SourceError::kResourceLimit, {}}; };
  try {
    ModuleReadSnapshot result;
    if (!constants.module || !constants.global_scope || !constants.global_scope->module_exports)
      return result;
    const auto& scope = *constants.global_scope;
    if (scope.module_slots != scope.module_exports->slot_count())
      return SourceFailure{SourceError::kInvalidEffect, {}};
    if (instructions.size() > UINT32_MAX) return limit();
    const auto initialized = [&](const analysis::SemanticInstruction& instruction) {
      if (result.work_units == budget.work) throw limit();
      ++result.work_units;
      const auto* load = std::get_if<analysis::ModuleLoad>(&instruction.operation);
      return load && load->space == analysis::ModuleSpace::kLocal &&
             load->slot.value < scope.module_slots &&
             load->slot.value < constants.module->local_exports.size() &&
             scope.module_exports->IsInitialized(load->slot);
    };
    std::size_t count = 0;
    for (const auto& instruction : instructions) count += initialized(instruction);
    if (count > budget.bytes / sizeof(analysis::LanguageLoadFact)) return limit();
    result.loads.reserve(count);
    if (result.AllocatedBytes() > budget.bytes) return limit();
    for (std::size_t i = 0; i < instructions.size(); ++i)
      if (initialized(instructions[i]))
        result.loads.push_back({analysis::InstructionId{static_cast<std::uint32_t>(i)}});
    return result;
  } catch (const SourceFailure& error) { return error;
  } catch (const std::bad_alloc&) { return limit();
  } catch (const std::length_error&) { return limit(); }
}
}  // namespace abcd::source
