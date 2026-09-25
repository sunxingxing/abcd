#include "conditional_value.h"

namespace abcd::source {
namespace {
bool IsConditionalOperand(const SourceExpression& expression) {
  if (const auto* scalar = std::get_if<analysis::ScalarExpression>(&expression))
    return std::holds_alternative<analysis::ScalarValue>(*scalar);
  // No creation naming, call owner or intermediate commits enter here.
  // ValueArm separately validates a member's composed receiver coordinates.
  return std::holds_alternative<StringExpression>(expression) ||
         std::holds_alternative<LocalExpression>(expression) ||
         std::holds_alternative<CapturedExpression>(expression) ||
         std::holds_alternative<MemberExpression>(expression);
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
  std::optional<analysis::InstructionId> guard;
  for (std::size_t i = block.first_instruction; i < end; ++i) {
    const auto& node = ast.statements[i];
    if (node.fused_read || node.fused_spill || node.fused_reload ||
        node.class_origin || node.literal_origin) return {};
    if (node.previous_origin) {
      if (guard || !std::holds_alternative<analysis::NoOperation>(node.operation)) return {};
      guard = analysis::InstructionId{static_cast<std::uint32_t>(i)};
    }
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
  if (!result) return {};
  const auto& assignment = std::get<SourceAssignment>(ast.statements[result->value].operation);
  const auto* member = std::get_if<MemberExpression>(&assignment.value);
  if (member && member->receiver) {
    const auto& receiver = *member->receiver;
    const auto source = receiver.binding.source.value;
    if (receiver.property != *result || source < block.first_instruction ||
        source >= result->value || receiver.binding.guard != guard) return {};
    if (guard && (guard->value != source + 1 || guard->value >= result->value ||
        ast.statements[guard->value].previous_origin != ast.statements[source].pc)) return {};
  } else if (guard) return {};
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
