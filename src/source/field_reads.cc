#include "field_reads.h"

#include "accumulator_liveness.h"

namespace abcd::source {
namespace {
bool HasOwner(const SourceStatement& node) {
  return node.constructor_arguments || node.fused_read || node.fused_spill ||
      node.fused_reload || node.previous_origin || node.class_origin || node.literal_origin;
}
}  // namespace
std::optional<SourceFailure> FoldFieldReads(
    const AstMethod& method, SourceAst& ast, std::size_t budget_bytes) {
  if (method.ssa.IsPhased() || ast.regions.catches || ast.argument_region) return {};
  for (const auto& block : method.flow.blocks)
    for (std::size_t e = 0; e < block.successor_count && e < block.successors.size(); ++e)
      if (block.successors[e].kind == analysis::FlowEdgeKind::kResume) return {};
  auto result = FindAccumulatorLiveness(method, ast, budget_bytes);
  if (const auto* failure = std::get_if<SourceFailure>(&result)) return *failure;
  const auto& live = std::get<AccumulatorLiveness>(result);
  for (const auto& block : method.flow.blocks) {
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    for (auto i = std::size_t(block.first_instruction); i + 1 < end; ++i) {
      auto& head = ast.statements[i];
      auto& tail = ast.statements[i + 1];
      if (HasOwner(head) || HasOwner(tail) ||
          live.accesses[i] != AccumulatorAccess::kWrite || live.after[i + 1]) continue;
      const auto* assignment = std::get_if<SourceAssignment>(&head.operation);
      const auto* read = assignment ? std::get_if<MemberExpression>(&assignment->value) : nullptr;
      auto* store = std::get_if<SourceMemberStore>(&tail.operation);
      const auto* value = store ? std::get_if<analysis::ScalarValue>(&store->value) : nullptr;
      if (!assignment || assignment->object_field ||
          assignment->result != AssignmentResult::kStored ||
          !std::holds_alternative<analysis::Accumulator>(assignment->target) ||
          !read || read->receiver || !std::holds_alternative<abc::RegisterId>(read->object) ||
          !store || !std::holds_alternative<abc::RegisterId>(store->object) ||
          !value || !std::holds_alternative<analysis::Accumulator>(*value)) continue;
      // A named Reference on a private register performs no getter/coercion.
      // Read the RHS property once, then PutValue, just as before. No computed
      // keys, captured cells, or exception observers may enter this proof.
      store->value = *read;
      head.operation = analysis::NoOperation{};
      tail.fused_read = head.pc;
      tail.read_distance = 1;
      ++i;
    }
  }
  return {};
}
}  // namespace abcd::source
