#include "member_predicates.h"

#include "slot_liveness.h"

namespace abcd::source {
namespace {
struct MemberPredicate {
  std::size_t source;
  std::size_t branch;
  abc::RegisterId target;
  MemberComparison comparison;
};
bool HasOwner(const SourceStatement& node) {
  return node.constructor_arguments || node.class_origin || node.literal_origin;
}
std::optional<MemberPredicate> Match(const SourceAst& ast,
                                    std::size_t source, std::size_t branch) {
  const auto& head = ast.statements[source];
  const auto& tail = ast.statements[branch];
  if (HasOwner(head) || head.fused_read || head.fused_spill || head.fused_reload ||
      head.previous_origin || HasOwner(tail) || tail.previous_origin || !tail.fused_read) return {};
  const auto* assignment = std::get_if<SourceAssignment>(&head.operation);
  const auto* target = assignment ? std::get_if<abc::RegisterId>(&assignment->target) : nullptr;
  const auto* member = assignment ? std::get_if<MemberExpression>(&assignment->value) : nullptr;
  if (!target || !member || member->receiver ||
      !std::holds_alternative<abc::RegisterId>(member->object) ||
      assignment->result != AssignmentResult::kStored || assignment->object_field) return {};
  const auto* jump = std::get_if<SourceConditionalBreak>(&tail.operation);
  const auto* loop = std::get_if<SourceConditionalContinue>(&tail.operation);
  const auto* condition = jump ? &jump->condition : loop ? &loop->condition : nullptr;
  const auto* input = jump ? &jump->input : loop ? &loop->input : nullptr;
  if (!condition || (condition->op != analysis::UnaryOperator::kToBoolean &&
                     condition->op != analysis::UnaryOperator::kLogicalNot) ||
      !input || !*input) return {};
  const auto* scalar = std::get_if<analysis::ScalarExpression>(&**input);
  const auto* binary = scalar ? std::get_if<analysis::BinaryExpression>(scalar) : nullptr;
  const auto* left = binary ? std::get_if<abc::RegisterId>(&binary->left) : nullptr;
  if (!left || *left != *target ||
      (binary->op != analysis::BinaryOperator::kStrictEqual &&
       binary->op != analysis::BinaryOperator::kStrictNotEqual) ||
      !(std::holds_alternative<analysis::SpecialValue>(binary->right) ||
        std::holds_alternative<analysis::IntegerLiteral>(binary->right) ||
        std::holds_alternative<analysis::Float64Bits>(binary->right))) return {};
  return MemberPredicate{source, branch, *target,
      {*member, binary->right,
       binary->op == analysis::BinaryOperator::kStrictEqual
           ? StrictEquality::kEqual : StrictEquality::kNotEqual,
       {static_cast<std::uint32_t>(source)}}};
}
template <typename Visitor>
void VisitCandidates(const AstMethod& method, SourceAst& ast, Visitor visit) {
  for (const auto& block : method.flow.blocks) {
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    if (end > ast.statements.size()) return;
    std::optional<std::size_t> previous;
    for (auto i = std::size_t(block.first_instruction); i < end; ++i) {
      const auto& node = ast.statements[i];
      if (const auto* call = std::get_if<SourceStagedCall>(&node.operation)) {
        if (call->call_statement.value < i || call->call_statement.value >= end) return;
        i = call->call_statement.value;
        previous.reset();
        continue;
      }
      if (const auto* array = std::get_if<SourceArrayLiteral>(&node.operation)) {
        if (array->elements.empty() || array->elements.back().value < i ||
            array->elements.back().value >= end) return;
        i = array->elements.back().value;
        previous.reset();
        continue;
      }
      if (HasOwner(node) || (ast.super_arguments &&
          i >= ast.super_arguments->first.value && i <= ast.super_arguments->call.value)) {
        previous.reset();
        continue;
      }
      if (std::holds_alternative<analysis::NoOperation>(node.operation)) {
        if (node.fused_read || node.fused_spill || node.fused_reload) previous.reset();
        continue;
      }
      if (previous)
        if (const auto candidate = Match(ast, *previous, i); candidate && !visit(*candidate)) return;
      previous = i;
    }
  }
}
}  // namespace

std::optional<SourceFailure> FoldMemberPredicates(
    const AstMethod& method, SourceAst& ast, std::size_t budget_bytes) {
  if (method.ssa.IsPhased() || ast.regions.catches || ast.argument_region) return {};
  for (const auto& block : method.flow.blocks)
    for (std::size_t e = 0; e < block.successor_count && e < block.successors.size(); ++e)
      if (block.successors[e].kind == analysis::FlowEdgeKind::kResume) return {};
  std::optional<abc::RegisterId> selected;
  VisitCandidates(method, ast, [&](const MemberPredicate& candidate) {
    selected = candidate.target;
    return false;
  });
  if (!selected) return {};
  // Bound this slice to one selected slot, regardless of register count.
  // Never loop over registers and rerun whole-CFG liveness per variable.
  auto result = FindSlotLiveness(method, ast, *selected, budget_bytes);
  if (const auto* failure = std::get_if<SourceFailure>(&result)) return *failure;
  const auto& live = std::get<SlotLiveness>(result);
  VisitCandidates(method, ast, [&](const MemberPredicate& candidate) {
    if (candidate.target != *selected || live.after[candidate.branch] ||
        live.accesses[candidate.source] != SlotAccess::kWrite) return true;
    auto& node = ast.statements[candidate.branch];
    auto* jump = std::get_if<SourceConditionalBreak>(&node.operation);
    auto* loop = std::get_if<SourceConditionalContinue>(&node.operation);
    auto& input = jump ? jump->input : loop->input;
    // Only empty PC anchors separate the property GetValue from the test.
    // The dead slot commit disappears; the getter still executes exactly once
    // before either successor. Existing comparison/reload origins stay intact.
    input = candidate.comparison;
    ast.statements[candidate.source].operation = analysis::NoOperation{};
    return true;
  });
  return {};
}
}  // namespace abcd::source
