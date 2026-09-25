#include "rest_binding.h"

#include "private_copy.h"

namespace abcd::source {
std::optional<abc::RegisterId> FindRestLocal(
    const analysis::SemanticMethod& method, const SourceAst& ast,
    std::uint16_t first_argument) {
  if (ast.statements.size() != method.instructions.size()) return {};
  std::optional<abc::RegisterId> local;
  std::optional<analysis::SsaValueId> definition;
  const auto values = method.ssa.Values();
  for (std::size_t i = 0; i < ast.statements.size(); ++i) {
    const auto& statement = ast.statements[i];
    const auto* assignment = std::get_if<SourceAssignment>(&statement.operation);
    const auto* rest = assignment
        ? std::get_if<analysis::RestCopy>(&assignment->value) : nullptr;
    if (!rest) continue;
    const auto* target = std::get_if<abc::RegisterId>(&assignment->target);
    if (local || !target || rest->first_argument != first_argument ||
        statement.fused_read || statement.fused_spill || statement.fused_reload ||
        statement.class_origin || statement.previous_origin) return {};
    const auto* original = std::get_if<analysis::RestCopy>(&method.instructions[i].operation);
    if (!original || original->first_argument != first_argument ||
        i + 1 >= ast.statements.size() ||
        !IsRetargetedCopyAnchor(ast.statements[i + 1], statement)) return {};
    const auto& saved = method.instructions[i + 1];
    const auto* copy = std::get_if<analysis::ScalarAssignment>(&saved.operation);
    const auto* destination = copy ? std::get_if<abc::RegisterId>(&copy->destination) : nullptr;
    const auto* input = copy ? std::get_if<analysis::ScalarValue>(&copy->expression) : nullptr;
    if (!destination || *destination != *target || !input ||
        !std::holds_alternative<analysis::Accumulator>(*input) ||
        copy->completion != analysis::CompletionEffect::kNormal ||
        !IsPureCopyStep(saved)) return {};
    const auto producer = method.ssa.Write({static_cast<std::uint32_t>(i)});
    const auto save = analysis::InstructionId{static_cast<std::uint32_t>(i + 1)};
    const auto reads = method.ssa.Reads(save);
    if (!producer || reads.size() != 1 || reads.front().value != *producer) return {};
    definition = method.ssa.Write(save);
    if (!definition || definition->value >= values.size()) return {};
    const auto* slot = std::get_if<abc::RegisterId>(&values[definition->value].slot);
    if (!slot || *slot != *target) return {};
    local = *target;
  }
  if (!local) return {};
  // Original entry copies, later overwrites and phis must not supply any
  // read of this local. This is one whole-method scan, not one walk per use.
  for (const auto& read : method.ssa.AllReads()) {
    if (read.value.value >= values.size()) return {};
    const auto* slot = std::get_if<abc::RegisterId>(&values[read.value.value].slot);
    if (slot && *slot == *local && read.value != *definition) return {};
  }
  return local;
}
}  // namespace abcd::source
