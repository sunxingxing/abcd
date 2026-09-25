#include "value_edits.h"

namespace abcd::analysis {
namespace {
// IDs and links are checked once by VerifyValueUses before a batch. Updating
// one occurrence is O(1); do not replace this with a scan of all value users.
void Unlink(ValueProgram& graph, UseId id) {
  auto& use = graph.uses[id.value];
  if (const auto* value = std::get_if<ValueId>(&use.operand)) {
    if (use.previous) graph.uses[use.previous->value].next = use.next;
    else graph.values[value->value].first_use = use.next;
    if (use.next) graph.uses[use.next->value].previous = use.previous;
  }
  use.previous.reset();
  use.next.reset();
}

bool HasControlOrOwnerRole(const ValueOperation& operation) {
  return std::holds_alternative<ValueBranch>(operation) ||
         std::holds_alternative<ValueJump>(operation) ||
         std::holds_alternative<ValueReturn>(operation) ||
         std::holds_alternative<ValueGuard>(operation) ||
         std::holds_alternative<ValueArrayAllocation>(operation);
}
}  // namespace

std::optional<ValueEditError> ReplaceUse(
    ValueProgram& graph, UseId id, ValueId replacement) {
  if (id.value >= graph.uses.size() ||
      graph.uses[id.value].life != ValueLife::kLive)
    return ValueEditError::kInvalidUse;
  if (replacement.value >= graph.values.size())
    return ValueEditError::kInvalidValue;
  if (const auto* result = std::get_if<OpResult>(
          &graph.values[replacement.value].origin);
      result && graph.ops[result->op.value].life != ValueLife::kLive)
    return ValueEditError::kInvalidValue;
  auto& use = graph.uses[id.value];
  if (const auto* old = std::get_if<ValueId>(&use.operand);
      old && *old == replacement) return {};
  Unlink(graph, id);
  use.operand = replacement;
  auto& value = graph.values[replacement.value];
  use.next = value.first_use;
  if (use.next) graph.uses[use.next->value].previous = id;
  value.first_use = id;
  return {};
}

std::optional<ValueEditError> ErasePureOp(ValueProgram& graph, OpId id) {
  if (id.value >= graph.ops.size() ||
      graph.ops[id.value].life != ValueLife::kLive)
    return ValueEditError::kInvalidOp;
  auto& op = graph.ops[id.value];
  if (op.normal != LanguageEffect::kNone ||
      op.before_throw != LanguageEffect::kNone ||
      op.exceptional != ExceptionalCompletion::kImpossible ||
      HasControlOrOwnerRole(op.operation))
    return ValueEditError::kObservable;
  if (op.result && graph.values[op.result->value].first_use)
    return ValueEditError::kLiveResult;
  if (op.origin.value >= graph.origins.size())
    return ValueEditError::kInvalidOrigin;
  for (std::uint32_t ordinal = 0; ordinal < op.use_count; ++ordinal) {
    const UseId use{op.first_use.value + ordinal};
    Unlink(graph, use);
    graph.uses[use.value].life = ValueLife::kRemoved;
  }
  op.life = ValueLife::kRemoved;
  graph.origins[op.origin.value].disposition = OriginDisposition::kElidedPure;
  return {};
}
}  // namespace abcd::analysis
