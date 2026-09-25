#include "super_arguments.h"

#include "private_copy.h"

namespace abcd::source {
namespace {
bool HasOrigin(const SourceStatement& statement) {
  return statement.fused_read || statement.fused_spill || statement.fused_reload ||
         statement.previous_origin || statement.class_origin || statement.literal_origin;
}
bool ArgumentCopies(const AstMethod& method, const SourceAst& ast,
                    std::size_t first, std::size_t call,
                    const analysis::SuperRange& range,
                    std::span<const std::size_t> uses) {
  const auto reads = method.ssa.Reads({static_cast<std::uint32_t>(call)});
  if (reads.size() != range.count) return false;
  for (std::size_t ordinal = 0; ordinal < range.count; ++ordinal) {
    const auto position = first + ordinal * kSuperArgumentCopyInstructions;
    const auto& node = ast.statements[position];
    const auto* assignment = std::get_if<SourceAssignment>(&node.operation);
    const auto* scalar = assignment
        ? std::get_if<analysis::ScalarExpression>(&assignment->value) : nullptr;
    const auto* value = scalar ? std::get_if<analysis::ScalarValue>(scalar) : nullptr;
    const auto* source = value ? std::get_if<abc::RegisterId>(value) : nullptr;
    const auto* target = assignment ? std::get_if<abc::RegisterId>(&assignment->target) : nullptr;
    if (!source || !target || target->value != range.first.value + ordinal ||
        assignment->result != AssignmentResult::kStored || assignment->object_field ||
        HasOrigin(node) ||
        (source->value >= range.first.value &&
         source->value - range.first.value < range.count) ||
        !IsRetargetedCopyAnchor(ast.statements[position + 1], node)) return false;
    const auto& original = method.instructions[position];
    const auto* load = std::get_if<analysis::ScalarAssignment>(&original.operation);
    const auto* input = load ? std::get_if<analysis::ScalarValue>(&load->expression) : nullptr;
    const auto* reg = input ? std::get_if<abc::RegisterId>(input) : nullptr;
    if (!reg || *reg != *source || !IsPureCopyStep(original) ||
        load->completion != analysis::CompletionEffect::kNormal ||
        !std::holds_alternative<analysis::Accumulator>(load->destination)) return false;
    const auto output = RetargetedOutput(method, ast, position, position +
        kSuperArgumentCopyInstructions, *target, uses);
    if (!output || output->value >= uses.size() || uses[output->value] != 1 ||
        reads[ordinal].value != *output) return false;
  }
  return true;
}
}  // namespace

std::optional<SuperArgumentRegion> ProveSuperArguments(
    const AstMethod& method, const SourceAst& ast,
    std::span<const std::size_t> uses) {
  if (method.ssa.IsPhased() || ast.regions.catches ||
      method.instructions.size() != ast.statements.size()) return {};
  std::optional<SuperArgumentRegion> result;
  bool seen = false;
  for (const auto& block : method.flow.blocks) {
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    if (end > ast.statements.size()) return {};
    for (std::size_t i = block.first_instruction; i < end; ++i) {
      const auto* original = std::get_if<analysis::SuperRange>(&method.instructions[i].operation);
      if (!original) continue;
      if (seen) return {};
      seen = true;
      const auto* assignment = std::get_if<SourceAssignment>(&ast.statements[i].operation);
      const auto* range = assignment ? std::get_if<analysis::SuperRange>(&assignment->value) : nullptr;
      if (!range || range->first != original->first || range->count != original->count ||
          !range->count || range->count >
              (i - block.first_instruction) / kSuperArgumentCopyInstructions) continue;
      const auto first = i - std::size_t(range->count) * kSuperArgumentCopyInstructions;
      if (ArgumentCopies(method, ast, first, i, *range, uses))
        result = SuperArgumentRegion{{static_cast<std::uint32_t>(first)},
                                     {static_cast<std::uint32_t>(i)}};
    }
  }
  return result;
}
}  // namespace abcd::source
