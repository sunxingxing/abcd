#include "value_edits.h"
#include <type_traits>

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

}  // namespace

bool CanErasePureOp(const ValueOp& op) {
  if (op.normal != LanguageEffect::kNone ||
      op.before_throw != LanguageEffect::kNone ||
      op.exceptional != ExceptionalCompletion::kImpossible) return false;
  // Explicit admission: new owner/control variants must not become erasable
  // merely because an effects field happened to be empty.
  return std::visit([](const auto& operation) {
    using T = std::decay_t<decltype(operation)>;
    if constexpr (std::is_same_v<T, ValueCopy> ||
                  std::is_same_v<T, ValueUnary> ||
                  std::is_same_v<T, ValueBinary> ||
                  std::is_same_v<T, ValueString> ||
                  std::is_same_v<T, ValueNop>) return true;
    else {
      static_assert(std::is_same_v<T, ValueConcurrentNotification> || std::is_same_v<T, ValueAwait> || std::is_same_v<T, ValueYield> || std::is_same_v<T, ValueNamedRead> ||
                    std::is_same_v<T, ValueDebugger> ||
                    std::is_same_v<T, ValueThisProperty> || std::is_same_v<T, ValueNewTarget> || std::is_same_v<T, ValueFrameThis> ||
                    std::is_same_v<T, ValueFrameFunction> ||
                    std::is_same_v<T, ValueGlobalObject> ||
                    std::is_same_v<T, ValueSymbolConstructor> ||
                    std::is_same_v<T, ValueRestArguments> ||
                    std::is_same_v<T, ValueArgumentSnapshot> ||
                    std::is_same_v<T, ValueModuleRead> ||
                    std::is_same_v<T, ValueModuleWrite> ||
                    std::is_same_v<T, ValueFunctionCreation> || std::is_same_v<T, ValueClassCreation> ||
                    std::is_same_v<T, ValueLexicalLoad> || std::is_same_v<T, ValueLexicalStore> ||
                    std::is_same_v<T, ValueEnvironmentPush> || std::is_same_v<T, ValueEnvironmentPop> ||
                    std::is_same_v<T, ValueCurrentEnvironment> ||
                    std::is_same_v<T, ValueGlobalRead> ||
                    std::is_same_v<T, ValueNamedWrite> ||
                    std::is_same_v<T, ValueOwnWrite> || std::is_same_v<T, ValueIndexedWrite> ||
                    std::is_same_v<T, ValueDefineAccessor> || std::is_same_v<T, ValueArraySpread> || std::is_same_v<T, ValueComputedOwnWrite> ||
                    std::is_same_v<T, ValuePropertyRead> ||
                    std::is_same_v<T, ValuePropertyWrite> || std::is_same_v<T, ValuePropertyDelete> || std::is_same_v<T, ValueIteratorAcquire> || std::is_same_v<T, ValueObjectCheck> || std::is_same_v<T, ValueEnumeration> ||
                    std::is_same_v<T, ValueBranch> ||
                    std::is_same_v<T, ValueJump> ||
                    std::is_same_v<T, ValueReturn> ||
                    std::is_same_v<T, ValueThrow> || std::is_same_v<T, ValueHole> ||
                    std::is_same_v<T, ValueGuard> ||
                    std::is_same_v<T, ValueInvoke> || std::is_same_v<T, ValueSuperCall> || std::is_same_v<T, ValueSuperSpread> || std::is_same_v<T, ValueSuperCheck> || std::is_same_v<T, ValueConstruct> || std::is_same_v<T, ValueConstructApply> ||
                    std::is_same_v<T, ValueArrayAllocation> || std::is_same_v<T, ValueObjectAllocation> ||
                    std::is_same_v<T, ValueIterResult>);
      return false;
    }
  }, op.operation);
}

std::optional<ValueEditError> ReplaceUse(
    ValueProgram& graph, UseId id, ValueId replacement) {
  if (id.value >= graph.uses.size() ||
      graph.uses[id.value].life != ValueLife::kLive)
    return ValueEditError::kInvalidUse;
  if (replacement.value >= graph.values.size() ||
      graph.values[replacement.value].life != ValueLife::kLive)
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

std::optional<ValueEditError> ReplaceLiteralUse(
    ValueProgram& graph, UseId id, ValueOperand literal) {
  if (id.value >= graph.uses.size() || graph.uses[id.value].life != ValueLife::kLive)
    return ValueEditError::kInvalidUse;
  if (std::holds_alternative<ValueId>(literal)) return ValueEditError::kInvalidValue;
  Unlink(graph, id);
  graph.uses[id.value].operand = literal;
  return {};
}

std::optional<ValueEditError> ErasePureOp(ValueProgram& graph, OpId id) {
  if (id.value >= graph.ops.size() ||
      graph.ops[id.value].life != ValueLife::kLive)
    return ValueEditError::kInvalidOp;
  auto& op = graph.ops[id.value];
  if (!CanErasePureOp(op))
    return ValueEditError::kObservable;
  if (op.result && graph.values[op.result->value].first_use)
    return ValueEditError::kLiveResult;
  if (op.origin.value >= graph.origins.size())
    return ValueEditError::kInvalidOrigin;
  for (std::uint32_t ordinal = 0; ordinal < op.use_count; ++ordinal) {
    const UseId use{op.first_use.value + ordinal};
    if (graph.uses[use.value].life == ValueLife::kRemoved) continue;
    Unlink(graph, use);
    graph.uses[use.value].life = ValueLife::kRemoved;
  }
  op.life = ValueLife::kRemoved;
  if (op.result) graph.values[op.result->value].life = ValueLife::kRemoved;
  graph.origins[op.origin.value].disposition = OriginDisposition::kElidedPure;
  return {};
}

std::optional<ValueEditError> EraseUnmarkedValues(
    ValueProgram& graph, std::span<const std::uint8_t> live_ops,
    std::span<const std::uint8_t> live_values) {
  if (live_ops.size() != graph.ops.size() ||
      live_values.size() != graph.values.size()) return ValueEditError::kInvalidOp;
  const auto retained = [&](ValueId id) {
    const auto& value = graph.values[id.value];
    if (const auto* result = std::get_if<OpResult>(&value.origin))
      return live_ops[result->op.value] != 0;
    return std::holds_alternative<EntryDefinition>(value.origin) ||
           live_values[id.value] != 0;
  };
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    const auto& op = graph.ops[i];
    if (op.life == ValueLife::kRemoved || live_ops[i]) continue;
    if (!CanErasePureOp(op)) return ValueEditError::kObservable;
    if (op.origin.value >= graph.origins.size()) return ValueEditError::kInvalidOrigin;
  }
  // Dead SCCs may have internal users. Check external users first, then detach
  // all dead owners' occurrences before deleting any definition.
  const auto owner_retained = [&](const OperandUse& use) {
    if (const auto* op = std::get_if<OpId>(&use.owner))
      return live_ops[op->value] != 0;
    return live_values[std::get<ValueId>(use.owner).value] != 0;
  };
  for (const auto& use : graph.uses) {
    if (use.life != ValueLife::kLive || !owner_retained(use)) continue;
    if (const auto* value = std::get_if<ValueId>(&use.operand);
        value && !retained(*value)) return ValueEditError::kLiveResult;
  }
  for (std::size_t i = 0; i < graph.uses.size(); ++i) {
    auto& use = graph.uses[i];
    if (use.life != ValueLife::kLive || owner_retained(use)) continue;
    Unlink(graph, UseId{static_cast<std::uint32_t>(i)});
    use.life = ValueLife::kRemoved;
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (graph.ops[i].life == ValueLife::kRemoved || live_ops[i]) continue;
    if (const auto error = ErasePureOp(graph, OpId{static_cast<std::uint32_t>(i)}))
      return error;
  }
  for (std::size_t i = 0; i < graph.values.size(); ++i) {
    auto& value = graph.values[i];
    if (std::holds_alternative<PhiDefinition>(value.origin) && !live_values[i])
      value.life = ValueLife::kRemoved;
  }
  return {};
}
}  // namespace abcd::analysis
