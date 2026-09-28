#include "completion_plan.h"

#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

#include "control_skeleton.h"

namespace abcd::source {
namespace {
using namespace analysis;
void Require(bool condition) { if (!condition) throw CompletionError::kInput; }
bool Same(const ValueOperand& a, const ValueOperand& b) {
  if (a.index() != b.index()) return false;
  return std::visit([&](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, SpecialValue>) return value == std::get<T>(b);
    else return value.value == std::get<T>(b).value;
  }, a);
}
bool CleanupOperation(const ValueOperation& operation) {
  // Lexical/module/closure protocols need their own dynamic-environment pairing;
  // identical depth/slot numbers are NOT proof of the same dynamic storage.
  return std::visit([](const auto& op) {
    using T = std::decay_t<decltype(op)>;
    return std::is_same_v<T, ValueCopy> || std::is_same_v<T, ValueUnary> ||
        std::is_same_v<T, ValueBinary> || std::is_same_v<T, ValueString> ||
        std::is_same_v<T, ValueNamedRead> || std::is_same_v<T, ValueNamedWrite> ||
        std::is_same_v<T, ValuePropertyRead> || std::is_same_v<T, ValuePropertyWrite> ||
        std::is_same_v<T, ValueInvoke> || std::is_same_v<T, ValueConstruct> ||
        std::is_same_v<T, ValueArrayAllocation> || std::is_same_v<T, ValueObjectAllocation> ||
        std::is_same_v<T, ValueGlobalRead> || std::is_same_v<T, ValueGlobalObject> ||
        std::is_same_v<T, ValueSymbolConstructor> || std::is_same_v<T, ValueFrameThis> ||
        std::is_same_v<T, ValueFrameFunction> || std::is_same_v<T, ValueDebugger>;
  }, operation);
}
struct Corridor { std::vector<ControlId> nodes; std::vector<OpId> operations; };
class CompletionBuilder {
 public:
  CompletionBuilder(const ValueProgram& graph, const ExceptionPlan& exceptions,
      const ExceptionControl& control, const EvaluationPlan& evaluation, ControlQuota& quota)
      : graph_(graph), exceptions_(exceptions), control_(control), evaluation_(evaluation), quota_(quota) {}
  CompletionPlan Build() {
    Require(!graph_.phases.empty() && graph_.revision == exceptions_.revision &&
        graph_.cfg_revision == exceptions_.cfg_revision && graph_.revision == control_.skeleton.revision &&
        graph_.cfg_revision == control_.skeleton.cfg_revision && graph_.revision == evaluation_.revision &&
        graph_.cfg_revision == evaluation_.cfg_revision && evaluation_.flow == EvaluationFlow::kPhased &&
        evaluation_.operations.size() == graph_.ops.size() &&
        (evaluation_.aliases.empty() || evaluation_.aliases.size() == graph_.values.size()));
    quota_.Size(incoming_, control_.skeleton.nodes.size());
    quota_.Size(visited_, control_.skeleton.nodes.size());
    quota_.Size(body_loops_, control_.skeleton.loops.size());
    quota_.Size(path_positions_, graph_.blocks.size());
    quota_.Size(peers_, graph_.values.size());
    quota_.Size(exits_, graph_.ops.size());
    CanonicalCopies();
    for (const auto& node : control_.skeleton.nodes) {
      quota_.Work();
      const auto count = [&](std::optional<ControlId> target) {
        if (target) { Require(target->value < incoming_.size()); ++incoming_[target->value]; }
      };
      count(node.next);
      if (node.arms) { count(node.arms->taken); count(node.arms->fallthrough); }
    }
    for (std::size_t e = 0; e < graph_.exceptional_exits.size(); ++e) {
      quota_.Work(); const auto op = graph_.exceptional_exits[e].op;
      Require(op.value < exits_.size() && !exits_[op.value]); exits_[op.value] = e;
    }
    Require(control_.regions.size() == exceptions_.regions.size());
    quota_.Size(regions_, control_.regions.size());
    Require(exceptions_.layout.source_order.size() == regions_.size());
    // Source order is outer-before-inner, independent of encoded TryId order.
    // Summarize each owned body/handler once; never replay descendant graphs
    // for every ancestor. Cross-region abrupt completions remain unsupported.
    for (auto i = exceptions_.layout.source_order.rbegin(); i != exceptions_.layout.source_order.rend(); ++i) {
      quota_.Work(); const auto r = i->value;
      Require(r < regions_.size() && !regions_[r].ready);
      stamp_ = static_cast<std::size_t>(r) + 1;
      const auto& region = control_.regions[r];
      const auto body = Body(*i, region.body);
      const auto handler = Body(*i, region.handler);
      regions_[r] = {body, handler, true};
    }
    CompletionPlan plan{graph_.revision, graph_.cfg_revision, {}};
    for (std::uint32_t r = 0; r < control_.regions.size(); ++r) {
      quota_.Work(); stamp_ = static_cast<std::size_t>(r) + 1;
      const auto& region = control_.regions[r];
      const auto next = control_.skeleton.nodes[region.wrapper.value].next;
      if (!next || !regions_[r].body) continue;
      Corridor normal, handler;
      if (!Path(*next, normal, false) || !Path(region.handler, handler, true)) continue;
      if (normal.operations.size() < 2 || handler.operations.size() != normal.operations.size()) continue;
      const auto returned = normal.operations.back(), thrown = handler.operations.back();
      const auto& normal_exit = graph_.ops[returned.value].operation;
      const auto& handler_exit = graph_.ops[thrown.value].operation;
      const bool preserve = std::holds_alternative<ValueReturn>(normal_exit) &&
          std::holds_alternative<ValueThrow>(handler_exit);
      const bool replace_return = std::holds_alternative<ValueReturn>(normal_exit) &&
          std::holds_alternative<ValueReturn>(handler_exit);
      const bool replace_throw = std::holds_alternative<ValueThrow>(normal_exit) &&
          std::holds_alternative<ValueThrow>(handler_exit);
      if (!preserve && !replace_return && !replace_throw) continue;
      const auto pending = Operand(returned, 0), exception = Operand(thrown, 0);
      const auto binding = exceptions_.regions[r].binding;
      if (!binding || (preserve && (!Same(exception, ValueOperand{binding->value}) ||
          !Available(pending, graph_.ops[normal.operations.front().value].block)))) continue;
      std::vector<CleanupPair> pairs;
      bool equal = true;
      for (std::size_t i = 0; i + 1 < normal.operations.size(); ++i) {
        quota_.Work(); const auto a = normal.operations[i], b = handler.operations[i];
        if (!Pair(a, b, {r})) { equal = false; break; }
        Append(pairs, CleanupPair{a, b});
      }
      if (!equal) continue;
      std::optional<PendingCompletion> replacement;
      if (!preserve) {
        if (!Pair(returned, thrown, {r}, true)) continue;
        Append(pairs, CleanupPair{returned, thrown});
        replacement = replace_return ? PendingCompletion{ReturnCompletion{pending}} :
            PendingCompletion{ThrowCompletion{pending}};
      }
      // An overwritten normal path need not invent a discarded source return.
      // Emit the actual protected effects and let finally supply its completion.
      const PendingCompletion incoming = preserve ? PendingCompletion{ReturnCompletion{pending}} :
          PendingCompletion{NormalCompletion{}};
      Append(plan.finalizers, FinalizerRegion{{r}, incoming, ThrowCompletion{binding->value},
          returned, thrown, std::move(normal.nodes), std::move(handler.nodes), std::move(pairs), replacement});
    }
    return plan;
  }
 private:
  template <class T> void Append(std::vector<T>& values, T value) {
    if (values.size() == values.capacity()) {
      if (values.size() > (std::numeric_limits<std::size_t>::max() - 1) / 2)
        throw CompletionError::kResourceLimit;
      const auto capacity = values.empty() ? 1 : values.size() * 2;
      quota_.Admit(capacity, sizeof(T)); values.reserve(capacity);
    }
    values.push_back(std::move(value));
  }
  ValueOperand Operand(OpId op, std::uint32_t index) const {
    const auto& operation = graph_.ops[op.value];
    Require(index < operation.use_count && operation.first_use.value + index < graph_.uses.size());
    const auto& operand = graph_.uses[operation.first_use.value + index].operand;
    if (const auto* value = std::get_if<ValueId>(&operand)) {
      Require(value->value < canonical_.size() && canonical_[value->value].has_value());
      return *canonical_[value->value];
    }
    return operand;
  }
  void CanonicalCopies() {
    // Unlike expression inlining, identity comparison may follow a catch
    // snapshot copy. This does not remove its source binding or move its read.
    // Memoization avoids a copy-chain walk for every operand/region.
    quota_.Size(canonical_, graph_.values.size());
    std::vector<std::uint8_t> active;
    std::vector<ValueId> pending;
    quota_.Size(active, graph_.values.size()); quota_.Size(pending, graph_.values.size());
    for (std::uint32_t v = 0; v < graph_.values.size(); ++v) {
      quota_.Work(); if (canonical_[v]) continue;
      ValueOperand current = ValueId{v}; std::size_t count = 0;
      while (const auto* value = std::get_if<ValueId>(&current)) {
        quota_.Work(); const auto id = *value; Require(id.value < canonical_.size());
        if (canonical_[id.value]) { current = *canonical_[id.value]; break; }
        Require(!active[id.value]);
        const auto* definition = std::get_if<OpResult>(&graph_.values[id.value].origin);
        if (!definition || !std::holds_alternative<ValueCopy>(graph_.ops[definition->op.value].operation)) {
          canonical_[id.value] = current; break;
        }
        const auto& op = graph_.ops[definition->op.value];
        Require(op.use_count == 1 && op.normal == LanguageEffect::kNone &&
            op.before_throw == LanguageEffect::kNone && op.exceptional == ExceptionalCompletion::kImpossible);
        active[id.value] = 1; pending[count++] = id;
        current = graph_.uses[op.first_use.value].operand;
      }
      while (count != 0) { quota_.Work(); canonical_[pending[--count].value] = current; }
    }
  }
  bool Body(TryId region, ControlId entry) {
    // Shared RegionPlan arms stop at their enclosing continuation. A missing
    // node.next at that boundary is a checked join, not an abrupt body exit.
    // Follow the same stop convention as VerifyControlSkeleton; never invent
    // a successor from instruction adjacency or cleanup PC proximity.
    struct Task { ControlId entry; std::optional<ControlId> stop; };
    std::vector<Task> pending;
    Append(pending, Task{entry, {}});
    while (!pending.empty()) {
      quota_.Work(); const auto task = pending.back(); pending.pop_back();
      const auto id = task.entry;
      Require(id.value < control_.skeleton.nodes.size());
      if (visited_[id.value] == stamp_) continue;
      visited_[id.value] = stamp_;
      const auto& node = control_.skeleton.nodes[id.value];
      if (const auto* wrapper = std::get_if<LoopControl>(&node.synthetic)) {
        const auto& loop = control_.skeleton.loops[wrapper->loop.value];
        body_loops_[wrapper->loop.value] = stamp_;
        // This loop is wholly in the protected body. Its transfers are already
        // checked against the common loop forest; completion only asks whether
        // their destination stays inside this try, not how to rediscover loops.
        if (node.next) Append(pending, Task{*node.next, task.stop});
        Append(pending, Task{loop.body, {}});
        continue;
      }
      if (const auto* transfer = std::get_if<LoopTransfer>(&node.synthetic)) {
        if (body_loops_[transfer->loop.value] != stamp_) return false;
        continue;
      }
      if (id.value >= control_.units.size()) return false;
      if (const auto* port = std::get_if<TryExitUnit>(&control_.units[id.value])) {
        if (port->region != region || node.next) return false;
        continue;
      }
      if (const auto* nested = std::get_if<TryControlUnit>(&control_.units[id.value])) {
        const auto& summary = regions_[nested->region.value];
        Require(summary.ready);
        if (!summary.body || !summary.handler) return false;
        if (node.next) Append(pending, Task{*node.next, task.stop});
        else if (control_.regions[nested->region.value].exit && !task.stop) return false;
        continue;
      }
      const auto* phase = std::get_if<PhaseControlUnit>(&control_.units[id.value]);
      Require(phase != nullptr);
      const auto& block = graph_.blocks[phase->block.value];
      bool throwing = false;
      for (std::uint32_t o = 0; o < block.op_count; ++o) {
        quota_.Work(); const auto& op = graph_.ops[block.first_op.value + o];
        if (std::holds_alternative<ValueReturn>(op.operation)) return false;
        throwing |= std::holds_alternative<ValueThrow>(op.operation);
      }
      if (node.next) Append(pending, Task{*node.next, task.stop});
      if (node.arms) {
        const auto join = node.next ? node.next : task.stop;
        if (node.arms->taken) Append(pending, Task{*node.arms->taken, join});
        if (node.arms->fallthrough) Append(pending, Task{*node.arms->fallthrough, join});
      }
      if (!node.next && !node.arms && !throwing && !task.stop) return false;
    }
    return true;
  }
  bool Path(ControlId entry, Corridor& path, bool handler) {
    auto cursor = std::optional{entry};
    while (cursor) {
      quota_.Work(); const auto id = *cursor;
      if (id.value >= control_.units.size()) return false;
      const auto* phase = std::get_if<PhaseControlUnit>(&control_.units[id.value]);
      const auto& node = control_.skeleton.nodes[id.value];
      if (!phase || node.arms || !std::holds_alternative<std::monostate>(node.synthetic) ||
          incoming_[id.value] != (handler && path.nodes.empty() ? 0U : 1U)) return false;
      if (path.nodes.size() >= control_.units.size()) return false;
      Append(path.nodes, id);
      if (!handler) path_positions_[phase->block.value] = {stamp_, path.nodes.size()};
      const auto& block = graph_.blocks[phase->block.value];
      for (std::uint32_t o = 0; o < block.op_count; ++o) {
        quota_.Work(); const OpId op{block.first_op.value + o};
        const auto kind = evaluation_.operations[op.value].kind;
        if (kind == EvaluationKind::kAlias || kind == EvaluationKind::kElidedPure ||
            kind == EvaluationKind::kRemoved || std::holds_alternative<ValueJump>(graph_.ops[op.value].operation) ||
            std::holds_alternative<ValueCopy>(graph_.ops[op.value].operation)) continue;
        Append(path.operations, op);
      }
      cursor = node.next;
    }
    return true;
  }
  bool Available(const ValueOperand& operand, WorkBlockId first) {
    const auto* value = std::get_if<ValueId>(&operand);
    if (!value) return true;
    const auto& definition = graph_.values[value->value].origin;
    std::optional<WorkBlockId> block;
    if (const auto* result = std::get_if<OpResult>(&definition)) {
      if (!result->commit) return false;
      block = *result->commit;
    } else if (const auto* phi = std::get_if<PhiDefinition>(&definition)) block = phi->block;
    else if (std::holds_alternative<CatchDefinition>(definition)) return false;
    if (!block) return true;
    const auto position = path_positions_[block->value];
    return position.stamp != stamp_ || position.index <= path_positions_[first.value].index;
  }
  bool Pair(OpId a, OpId b, TryId region, bool completion = false) {
    const auto& left = graph_.ops[a.value]; const auto& right = graph_.ops[b.value];
    const bool supported = completion ? (std::holds_alternative<ValueReturn>(left.operation) ||
        std::holds_alternative<ValueThrow>(left.operation)) : CleanupOperation(left.operation);
    if (!supported || left.operation != right.operation ||
        left.normal != right.normal || left.before_throw != right.before_throw ||
        left.exceptional != right.exceptional || left.use_count != right.use_count ||
        left.result.has_value() != right.result.has_value() || exits_[a.value].has_value() != exits_[b.value].has_value()) return false;
    if (exits_[a.value]) {
      const auto& x = graph_.exceptional_exits[*exits_[a.value]];
      const auto& y = graph_.exceptional_exits[*exits_[b.value]];
      if (x.handler != y.handler || x.before_throw != y.before_throw ||
          (x.handler && exceptions_.regions[region.value].binding &&
           x.handler->entry == exceptions_.regions[region.value].binding->block)) return false;
    }
    for (std::uint32_t u = 0; u < left.use_count; ++u) {
      quota_.Work();
      const auto& x = graph_.uses[left.first_use.value + u];
      const auto& y = graph_.uses[right.first_use.value + u];
      if (x.role != y.role || x.purpose != y.purpose) return false;
      auto second = Operand(b, u);
      if (const auto* value = std::get_if<ValueId>(&second); value && peers_[value->value].stamp == stamp_)
        second = peers_[value->value].value;
      if (!Same(Operand(a, u), second)) return false;
    }
    if (left.result) peers_[right.result->value] = {stamp_, *left.result};
    return true;
  }
  struct Position { std::size_t stamp = 0, index = 0; };
  struct Peer { std::size_t stamp = 0; ValueId value; };
  struct RegionCompletion { bool body = false, handler = false, ready = false; };
  const ValueProgram& graph_;
  const ExceptionPlan& exceptions_;
  const ExceptionControl& control_;
  const EvaluationPlan& evaluation_;
  ControlQuota& quota_;
  std::size_t stamp_ = 0;
  std::vector<std::size_t> incoming_, visited_, body_loops_;
  std::vector<RegionCompletion> regions_;
  std::vector<Position> path_positions_;
  std::vector<Peer> peers_;
  std::vector<std::optional<ValueOperand>> canonical_;
  std::vector<std::optional<std::size_t>> exits_;
};
}  // namespace
std::variant<CompletionPlan, CompletionError> BuildCompletionPlan(const ValueProgram& graph,
    const ExceptionPlan& exceptions, const ExceptionControl& control,
    const EvaluationPlan& evaluation, SemanticBudget budget) {
  try {
    ControlQuota quota(budget);
    return CompletionBuilder(graph, exceptions, control, evaluation, quota).Build();
  } catch (CompletionError error) { return error;
  } catch (ControlError error) {
    return error == ControlError::kWorkLimit ? CompletionError::kWorkLimit : CompletionError::kResourceLimit;
  } catch (const std::bad_alloc&) { return CompletionError::kResourceLimit;
  } catch (const std::length_error&) { return CompletionError::kResourceLimit; }
}
}  // namespace abcd::source
