#include "boolean_diamond.h"

namespace abcd::source {
namespace {
std::optional<BooleanDiamond> BooleanArm(const SourceAst& ast,
    const analysis::NormalFlow& flow, analysis::BlockId id,
    analysis::BlockId join) {
  if (id.value >= flow.blocks.size()) return {};
  const auto& block = flow.blocks[id.value];
  const auto end = std::size_t(block.first_instruction) + block.instruction_count;
  if (!block.instruction_count || end > ast.statements.size() ||
      block.successor_count != 1 || block.successors.front().target != join ||
      (block.transfer != abc::ControlTransfer::kJump &&
       block.transfer != abc::ControlTransfer::kFallthrough)) return {};
  std::optional<BooleanDiamond> result;
  for (std::size_t i = block.first_instruction; i < end; ++i) {
    const auto& node = ast.statements[i];
    if (node.fused_read || node.fused_spill || node.fused_reload ||
        node.previous_origin || node.class_origin || node.literal_origin) return {};
    if (i + 1 == end && block.transfer == abc::ControlTransfer::kJump) {
      const auto* jump = std::get_if<SourceBreak>(&node.operation);
      if (!jump || jump->target != join) return {};
      continue;
    }
    if (std::holds_alternative<analysis::NoOperation>(node.operation)) continue;
    const auto* assignment = std::get_if<SourceAssignment>(&node.operation);
    const auto* scalar = assignment
        ? std::get_if<analysis::ScalarExpression>(&assignment->value) : nullptr;
    const auto* value = scalar ? std::get_if<analysis::ScalarValue>(scalar) : nullptr;
    const auto* boolean = value ? std::get_if<analysis::SpecialValue>(value) : nullptr;
    if (result || !assignment || assignment->result != AssignmentResult::kStored ||
        assignment->object_field || !boolean ||
        (*boolean != analysis::SpecialValue::kTrue &&
         *boolean != analysis::SpecialValue::kFalse)) return {};
    result = BooleanDiamond{assignment->target,
                             *boolean == analysis::SpecialValue::kTrue};
  }
  return result;
}
}  // namespace

std::optional<BooleanDiamond> ProveBooleanDiamond(
    const SourceAst& ast, const analysis::NormalFlow& flow,
    const analysis::RegionIf& region) {
  if (!region.alternate || region.body == *region.alternate ||
      region.join.value >= flow.blocks.size()) return {};
  const auto body = BooleanArm(ast, flow, region.body, region.join);
  const auto alternate = BooleanArm(ast, flow, *region.alternate, region.join);
  if (!body || !alternate || body->body_value == alternate->body_value ||
      body->target.index() != alternate->target.index()) return {};
  if (const auto* reg = std::get_if<abc::RegisterId>(&body->target);
      reg && *reg != std::get<abc::RegisterId>(alternate->target)) return {};
  return body;
}
}  // namespace abcd::source
