#include "overwritten_accumulator.h"

namespace abcd::source {
namespace {
const analysis::ScalarValue* PureStoredValue(const SourceStatement& node) {
  const auto* assignment = std::get_if<SourceAssignment>(&node.operation);
  if (!assignment || assignment->result != AssignmentResult::kStored ||
      assignment->object_field) return nullptr;
  const auto* expression = std::get_if<analysis::ScalarExpression>(&assignment->value);
  const auto* value = expression
      ? std::get_if<analysis::ScalarValue>(expression) : nullptr;
  if (!value) return nullptr;
  // Only private reads and primitive constants. In particular, allocation,
  // name lookup, property access and coercion are not dead-write candidates.
  return std::holds_alternative<analysis::Accumulator>(*value) ||
         std::holds_alternative<abc::RegisterId>(*value) ||
         std::holds_alternative<analysis::SpecialValue>(*value) ||
         std::holds_alternative<analysis::IntegerLiteral>(*value) ||
         std::holds_alternative<analysis::Float64Bits>(*value) ? value : nullptr;
}
}  // namespace

void ElideOverwrittenAccumulator(const AstMethod& method, SourceAst& ast) {
  if (method.ssa.IsPhased() || ast.regions.catches || ast.argument_region ||
      ast.super_arguments || method.instructions.size() != ast.statements.size()) return;
  for (const auto& block : method.flow.blocks) {
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    if (end > ast.statements.size()) return;
    std::optional<std::size_t> pending;
    for (auto i = std::size_t(block.first_instruction); i < end; ++i) {
      auto& node = ast.statements[i];
      if (const auto* literal = std::get_if<SourceArrayLiteral>(&node.operation)) {
        if (literal->elements.empty() || literal->elements.back().value < i ||
            literal->elements.back().value >= end) return;
        i = literal->elements.back().value;
        pending.reset();
        continue;
      }
      if (const auto* staged = std::get_if<SourceStagedCall>(&node.operation)) {
        if (staged->call_statement.value < i || staged->call_statement.value >= end) return;
        i = staged->call_statement.value;
        pending.reset();
        continue;
      }
      if (node.constructor_arguments || node.fused_read || node.fused_spill ||
          node.fused_reload || node.literal_origin) {
        pending.reset();
        continue;
      }
      // Class/previous-origin no-ops only alias an earlier emitted span.
      // Preserve that provenance even when its write becomes zero-width.
      if (std::holds_alternative<analysis::NoOperation>(node.operation)) continue;
      if (node.previous_origin) { pending.reset(); continue; }
      const auto* value = PureStoredValue(node);
      if (!value) { pending.reset(); continue; }
      const bool reads_acc = std::holds_alternative<analysis::Accumulator>(*value);
      if (reads_acc) pending.reset();
      const auto& assignment = std::get<SourceAssignment>(node.operation);
      if (!std::holds_alternative<analysis::Accumulator>(assignment.target)) continue;
      // This is a physical emitted-write proof. Retargeting/class assembly has
      // changed reads since SSA construction; never consult stale SSA counts.
      if (pending) ast.statements[*pending].operation = analysis::NoOperation{};
      pending = i;
    }
  }
}
}  // namespace abcd::source
