#include "store_snapshots.h"

#include "private_copy.h"
#include "register_reads.h"

namespace abcd::source {
namespace {
struct StoreSnapshot {
  std::size_t statement;
  abc::RegisterId target;
  abc::RegisterId source;
};
bool HasOwner(const SourceStatement& node) {
  return node.constructor_arguments || node.fused_read || node.fused_spill ||
      node.fused_reload || node.class_origin || node.literal_origin;
}
std::optional<StoreSnapshot> Snapshot(const AstMethod& method,
                                    const SourceStatement& node,
                                    std::size_t position) {
  const auto* assignment = std::get_if<SourceAssignment>(&node.operation);
  const auto* target = assignment
      ? std::get_if<abc::RegisterId>(&assignment->target) : nullptr;
  const auto* expression = assignment
      ? std::get_if<analysis::ScalarExpression>(&assignment->value) : nullptr;
  const auto* value = expression
      ? std::get_if<analysis::ScalarValue>(expression) : nullptr;
  const auto* source = value ? std::get_if<abc::RegisterId>(value) : nullptr;
  if (!target || !source || *source == *target || assignment->object_field ||
      assignment->result != AssignmentResult::kStored || node.previous_origin ||
      !IsPureCopyStep(method.instructions[position])) return {};
  return StoreSnapshot{position, *target, *source};
}
bool ReadsStoreValue(
    const std::variant<analysis::ScalarValue, StringExpression>& stored,
    abc::RegisterId reg) {
  const auto* value = std::get_if<analysis::ScalarValue>(&stored);
  return value && ReadsRegister(*value, reg);
}
}  // namespace

void FoldStoreSnapshots(const AstMethod& method, SourceAst& ast) {
  if (method.ssa.IsPhased() || ast.regions.catches || ast.argument_region ||
      ast.statements.size() != method.instructions.size()) return;
  for (const auto& block : method.flow.blocks) {
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    if (end > ast.statements.size()) return;
    std::optional<StoreSnapshot> pending;
    for (auto i = std::size_t(block.first_instruction); i < end; ++i) {
      auto& node = ast.statements[i];
      if (ast.super_arguments && i >= ast.super_arguments->first.value &&
          i <= ast.super_arguments->call.value) {
        pending.reset();
        continue;
      }
      // Owners print their children out of flat statement order. Do not enter
      // their intervals, even if an individual child looks like a plain copy.
      if (const auto* call = std::get_if<SourceStagedCall>(&node.operation)) {
        if (call->call_statement.value < i || call->call_statement.value >= end) return;
        i = call->call_statement.value;
        pending.reset();
        continue;
      }
      if (const auto* array = std::get_if<SourceArrayLiteral>(&node.operation)) {
        if (array->elements.empty() || array->elements.back().value < i ||
            array->elements.back().value >= end) return;
        i = array->elements.back().value;
        pending.reset();
        continue;
      }
      if (HasOwner(node)) { pending.reset(); continue; }
      if (std::holds_alternative<analysis::NoOperation>(node.operation)) continue;
      if (node.previous_origin) { pending.reset(); continue; }
      if (auto* store = std::get_if<SourceMemberStore>(&node.operation)) {
        if (pending && ReadsRegister(store->object, pending->target)) {
          // Only a private value read moves. Property Set, RHS evaluation,
          // getters and throws stay at their original source positions.
          store->object = pending->source;
        }
        if (pending && ReadsStoreValue(store->value, pending->target)) pending.reset();
        continue;
      }
      if (const auto* store = std::get_if<SourceOwnStore>(&node.operation)) {
        // Preserve the private-object certificate and its receiver verbatim.
        // An unrelated property write cannot mutate a private register slot.
        if (pending && (store->write.object == pending->target ||
                        ReadsStoreValue(store->value, pending->target))) pending.reset();
        continue;
      }
      const auto* assignment = std::get_if<SourceAssignment>(&node.operation);
      if (!assignment || assignment->object_field ||
          assignment->result == AssignmentResult::kArgumentOnly) {
        pending.reset();
        continue;
      }
      if (pending && ReadsRegister(assignment->value, pending->target) !=
                         RegisterRead::kAbsent) pending.reset();
      const auto* target = assignment->result == AssignmentResult::kStored
          ? std::get_if<abc::RegisterId>(&assignment->target) : nullptr;
      if (pending && target) {
        if (*target == pending->target) {
          // All intervening reads were replaced or absent. A real overwrite
          // kills the old value before any successor can observe it. Never
          // infer this from stale original SSA counts or a block-end alone.
          ast.statements[pending->statement].operation = analysis::NoOperation{};
          pending.reset();
        } else if (*target == pending->source) {
          pending.reset();
        }
      }
      if (!pending) pending = Snapshot(method, node, i);
    }
  }
}
}  // namespace abcd::source
