#include "conditional_value.h"

namespace abcd::source {
namespace {
bool IsConditionalOperand(const SourceExpression& expression) {
  if (const auto* scalar = std::get_if<analysis::ScalarExpression>(&expression))
    return std::holds_alternative<analysis::ScalarValue>(*scalar);
  // No creation naming, call owner, composed source origins or intermediate
  // commits enter here. Binding reads keep their domain checks at emission.
  return std::holds_alternative<StringExpression>(expression) ||
         std::holds_alternative<LocalExpression>(expression) ||
         std::holds_alternative<CapturedExpression>(expression);
}
std::optional<bool> BooleanValue(const SourceExpression& expression) {
  const auto* scalar = std::get_if<analysis::ScalarExpression>(&expression);
  const auto* value = scalar ? std::get_if<analysis::ScalarValue>(scalar) : nullptr;
  const auto* boolean = value ? std::get_if<analysis::SpecialValue>(value) : nullptr;
  if (!boolean || (*boolean != analysis::SpecialValue::kTrue &&
                   *boolean != analysis::SpecialValue::kFalse)) return {};
  return *boolean == analysis::SpecialValue::kTrue;
}
std::optional<analysis::InstructionId> ValueArm(const SourceAst& ast,
    const analysis::NormalFlow& flow, analysis::BlockId id,
    analysis::BlockId join) {
  if (id.value >= flow.blocks.size()) return {};
  const auto& block = flow.blocks[id.value];
  const auto end = std::size_t(block.first_instruction) + block.instruction_count;
  if (!block.instruction_count || end > ast.statements.size() ||
      block.successor_count != 1 || block.successors.front().target != join ||
      (block.transfer != abc::ControlTransfer::kJump &&
       block.transfer != abc::ControlTransfer::kFallthrough)) return {};
  std::optional<analysis::InstructionId> result;
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
    if (result || !assignment || assignment->result != AssignmentResult::kStored ||
        assignment->object_field || !IsConditionalOperand(assignment->value)) return {};
    result = analysis::InstructionId{static_cast<std::uint32_t>(i)};
  }
  return result;
}
}  // namespace

std::optional<ConditionalValue> ProveConditionalValue(
    const SourceAst& ast, const analysis::NormalFlow& flow,
    const analysis::RegionIf& region) {
  if (!region.alternate || region.body == *region.alternate ||
      region.join.value >= flow.blocks.size()) return {};
  const auto body = ValueArm(ast, flow, region.body, region.join);
  const auto alternate = ValueArm(ast, flow, *region.alternate, region.join);
  if (!body || !alternate) return {};
  const auto& left = std::get<SourceAssignment>(ast.statements[body->value].operation);
  const auto& right = std::get<SourceAssignment>(ast.statements[alternate->value].operation);
  if (left.target.index() != right.target.index()) return {};
  if (const auto* reg = std::get_if<abc::RegisterId>(&left.target);
      reg && *reg != std::get<abc::RegisterId>(right.target)) return {};
  const auto left_boolean = BooleanValue(left.value);
  const auto right_boolean = BooleanValue(right.value);
  const auto polarity = left_boolean && right_boolean && *left_boolean != *right_boolean
      ? left_boolean : std::nullopt;
  return ConditionalValue{left.target, *body, *alternate, polarity};
}
}  // namespace abcd::source
