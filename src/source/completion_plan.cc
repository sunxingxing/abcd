#include "completion_plan.h"

#include <algorithm>
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
        std::is_same_v<T, ValueFrameFunction> || std::is_same_v<T, ValueDebugger> ||
        std::is_same_v<T, ValueYield>;
  }, operation);
}
bool SameCleanupOperation(const ValueOperation& a, const ValueOperation& b) {
  // Yield sites have different protocol PCs/registers, not different language
  // operators. The owner normalizes each site; Pair below must still match SSA
  // inputs, effects and throw dispatch. Both origins remain in the tree.
  if (const auto* yield = std::get_if<ValueYield>(&a)) {
    const auto* other = std::get_if<ValueYield>(&b);
    return other && yield->origin.form == other->origin.form;
  }
  return a == b;
}
struct Corridor {
  std::vector<ControlId> nodes;
  std::vector<OpId> operations;
  std::optional<ControlId> transfer;
};
struct SharedCompletion {
  bool valid = true;
  bool uncaught = false;
  std::optional<WorkBlockId> handler;
  std::optional<LoopId> loop;
};
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
    quota_.Size(shared_completions_, control_.regions.size());
    Require(exceptions_.layout.source_order.size() == regions_.size());
    SharedGuards();
    // Source order is outer-before-inner, independent of encoded TryId order.
    // Summarize each owned body/handler once; never replay descendant graphs
    // for every ancestor. Shared summaries retain unresolved ancestor-loop
    // transfers until the owning protected/handler scope consumes them.
    for (auto i = exceptions_.layout.source_order.rbegin(); i != exceptions_.layout.source_order.rend(); ++i) {
      quota_.Work(); const auto r = i->value;
      Require(r < regions_.size() && !regions_[r].ready);
      stamp_ = static_cast<std::size_t>(r) + 1;
      const auto& region = control_.regions[r];
      const auto body = Body(*i, region.body);
      const auto handler = Body(*i, region.handler);
      if (evaluation_.holes) {
        auto shared = SharedScope(*i, region.body, false);
        Merge(shared, SharedScope(*i, region.handler, true));
        shared_completions_[r] = shared;
      }
      regions_[r] = {body, handler, true};
    }
    CompletionPlan plan{graph_.revision, graph_.cfg_revision, {}};
    for (std::uint32_t r = 0; r < control_.regions.size(); ++r) {
      quota_.Work(); stamp_ = static_cast<std::size_t>(r) + 1;
      const auto& region = control_.regions[r];
      const auto next = control_.skeleton.nodes[region.wrapper.value].next;
      const auto& shared_completion = shared_completions_[r];
      if (next && shared_guards_[r] && shared_completion.valid &&
          !shared_completion.uncaught && !shared_completion.handler && !shared_completion.loop) {
        const auto binding = exceptions_.regions[r].binding;
        Require(binding.has_value());
        FinalizerRegion shared{{r},
                               NormalCompletion{},
                               ThrowCompletion{binding->value},
                               {},
                               {},
                               {},
                               {},
                               {},
                               {}};
        shared.shared_guard = shared_guards_[r];
        Append(plan.finalizers, std::move(shared));
        continue;
      }
      if (!next) {
        // A bare abrupt suffix is consumed into the protected body's control
        // tree, not exposed as wrapper.next. Only straight tails qualify here;
        // moving a nonthrowing jump out of try preserves all body effects.
        Corridor normal, handler;
        const auto binding = exceptions_.regions[r].binding;
        if (!binding || !Path(region.body, normal, true) ||
            !Path(region.handler, handler, true) || !normal.transfer ||
            !handler.transfer || !handler.operations.empty()) continue;
        bool suspended = false;
        for (const auto op : normal.operations) {
          quota_.Work(); suspended |= std::holds_alternative<ValueYield>(graph_.ops[op.value].operation);
        }
        if (suspended) continue;
        const auto& left = std::get<LoopTransfer>(control_.skeleton.nodes[normal.transfer->value].synthetic);
        const auto& right = std::get<LoopTransfer>(control_.skeleton.nodes[handler.transfer->value].synthetic);
        if (left.loop != right.loop || left.kind != right.kind) continue;
        const PendingCompletion replacement = left.kind == LoopTransferKind::kBreak ?
            PendingCompletion{BreakCompletion{left.loop}} : PendingCompletion{ContinueCompletion{left.loop}};
        Append(plan.finalizers, FinalizerRegion{{r}, NormalCompletion{}, ThrowCompletion{binding->value},
            {}, {}, std::move(normal.nodes), std::move(handler.nodes), {}, replacement,
            CleanupTransferPair{*normal.transfer, *handler.transfer}, CleanupPlacement::kBodyTail});
        continue;
      }
      if (!regions_[r].body) continue;
      Corridor normal, handler;
      if (!Path(region.handler, handler, true)) continue;
      const bool complete_normal = Path(*next, normal, false) && !normal.transfer;
      if (normal.transfer && handler.transfer) {
        const auto& left = std::get<LoopTransfer>(control_.skeleton.nodes[normal.transfer->value].synthetic);
        const auto& right = std::get<LoopTransfer>(control_.skeleton.nodes[handler.transfer->value].synthetic);
        const auto binding = exceptions_.regions[r].binding;
        if (!binding || left.loop != right.loop || left.kind != right.kind ||
            normal.operations.size() != handler.operations.size()) continue;
        std::vector<CleanupPair> pairs;
        bool equal = true;
        for (std::size_t i = 0; i < normal.operations.size(); ++i) {
          quota_.Work(); const auto a = normal.operations[i], b = handler.operations[i];
          if (!Pair(a, b, {r})) { equal = false; break; }
          Append(pairs, CleanupPair{a, b});
        }
        if (!equal) continue;
        const PendingCompletion replacement = left.kind == LoopTransferKind::kBreak ?
            PendingCompletion{BreakCompletion{left.loop}} : PendingCompletion{ContinueCompletion{left.loop}};
        // A finalizer may consist solely of the overriding loop transfer.
        // Do not invent an opcode anchor or require an unrelated cleanup call.
        const auto normal_exit = pairs.empty() ? std::nullopt : std::optional{pairs.back().normal};
        const auto exceptional_exit = pairs.empty() ? std::nullopt : std::optional{pairs.back().exceptional};
        Append(plan.finalizers, FinalizerRegion{{r}, NormalCompletion{}, ThrowCompletion{binding->value},
            normal_exit, exceptional_exit, std::move(normal.nodes), std::move(handler.nodes),
            std::move(pairs), replacement, CleanupTransferPair{*normal.transfer, *handler.transfer}});
        continue;
      }
      // A normal completion continues after the paired cleanup prefix; those
      // following operations are not part of finally and must stay outside it.
      // Path retains its straight prefix at a branch/region boundary. Once the
      // entire cleanup fits there, the existing control tree owns the suffix.
      if (handler.operations.size() >= 2 &&
          ((!complete_normal && normal.operations.size() >= handler.operations.size() - 1) ||
           normal.operations.size() > handler.operations.size())) {
        const auto thrown = handler.operations.back();
        const auto binding = exceptions_.regions[r].binding;
        if (!binding || !std::holds_alternative<ValueThrow>(graph_.ops[thrown.value].operation) ||
            !Same(Operand(thrown, 0), ValueOperand{binding->value})) continue;
        std::vector<CleanupPair> pairs;
        bool equal = true;
        for (std::size_t i = 0; i + 1 < handler.operations.size(); ++i) {
          quota_.Work();
          const auto a = normal.operations[i], b = handler.operations[i];
          if (!Pair(a, b, {r})) { equal = false; break; }
          Append(pairs, CleanupPair{a, b});
        }
        if (!equal) continue;
        const auto last = pairs.back().normal;
        Append(plan.finalizers, FinalizerRegion{{r}, NormalCompletion{}, ThrowCompletion{binding->value},
            last, thrown, std::move(normal.nodes), std::move(handler.nodes), std::move(pairs), {}});
        continue;
      }
      if (!complete_normal) continue;
      if (normal.operations.empty() || handler.operations.size() != normal.operations.size()) continue;
      const auto returned = normal.operations.back(), thrown = handler.operations.back();
      const auto& normal_exit = graph_.ops[returned.value].operation;
      const auto& handler_exit = graph_.ops[thrown.value].operation;
      const auto binding = exceptions_.regions[r].binding;
      const bool preserve = (std::holds_alternative<ValueReturn>(normal_exit) ||
          std::holds_alternative<ValueThrow>(normal_exit)) &&
          std::holds_alternative<ValueThrow>(handler_exit) && binding &&
          Same(Operand(thrown, 0), ValueOperand{binding->value});
      const bool replace_return = std::holds_alternative<ValueReturn>(normal_exit) &&
          std::holds_alternative<ValueReturn>(handler_exit);
      const bool replace_throw = std::holds_alternative<ValueThrow>(normal_exit) &&
          std::holds_alternative<ValueThrow>(handler_exit);
      if (!preserve && !replace_return && !replace_throw) continue;
      // A replacement terminal is itself the finalizer action. Preserving an
      // incoming completion still needs actual cleanup before its terminal.
      if (preserve && normal.operations.size() < 2) continue;
      const auto pending = Operand(returned, 0);
      if (!binding || (preserve &&
          !Available(pending, graph_.ops[normal.operations.front().value].block))) continue;
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
      const PendingCompletion incoming = !preserve ? PendingCompletion{NormalCompletion{}} :
          std::holds_alternative<ValueThrow>(normal_exit) ? PendingCompletion{ThrowCompletion{pending}} :
          PendingCompletion{ReturnCompletion{pending}};
      Append(plan.finalizers, FinalizerRegion{{r}, incoming, ThrowCompletion{binding->value},
          returned, thrown, std::move(normal.nodes), std::move(handler.nodes), std::move(pairs), replacement});
    }
    YieldReturns(plan);
    return plan;
  }
 private:
  void YieldReturns(CompletionPlan& plan) {
    bool needed = false;
    for (const auto& op : graph_.ops) {
      quota_.Work();
      const auto* yield = std::get_if<ValueYield>(&op.operation);
      needed |= yield && yield->origin.return_route.has_value();
    }
    if (!needed) return;
    struct Site { abc::CodeOffset pc; OpId operation; };
    std::vector<Site> sites;
    quota_.Size(sites, graph_.ops.size());
    for (std::uint32_t i = 0; i < graph_.ops.size(); ++i) {
      quota_.Work(); const auto& origin = graph_.origins[graph_.ops[i].origin.value];
      sites[i] = {origin.original.value_or(origin.pc), {i}};
    }
    std::sort(sites.begin(), sites.end(), [&](const auto& a, const auto& b) {
      quota_.Work(); return a.pc < b.pc;
    });
    const auto find = [&](abc::CodeOffset pc) {
      const auto at = std::lower_bound(sites.begin(), sites.end(), pc, [&](const auto& site, auto target) {
        quota_.Work(); return site.pc < target;
      });
      Require(at != sites.end() && at->pc == pc); return at->operation;
    };
    for (std::uint32_t i = 0; i < graph_.ops.size(); ++i) {
      quota_.Work(); const auto* yield = std::get_if<ValueYield>(&graph_.ops[i].operation);
      if (!yield || !yield->origin.return_route) continue;
      const auto& route = *yield->origin.return_route;
      const auto branch = find(route.branch);
      const auto completion = route.completion ? std::optional{find(*route.completion)} : std::nullopt;
      Require(std::holds_alternative<ValueBranch>(graph_.ops[branch.value].operation) &&
          (!completion || std::holds_alternative<ValueReturn>(graph_.ops[completion->value].operation)));
      Append(plan.yield_returns, CompletionPlan::YieldReturn{{i}, branch, completion});
    }
  }
  void SharedGuards() {
    quota_.Size(shared_guards_, exceptions_.regions.size());
    quota_.Size(shared_visits_, control_.skeleton.nodes.size());
    quota_.Size(shared_loops_, control_.skeleton.loops.size());
    quota_.Size(loop_depths_, control_.skeleton.loops.size());
    std::vector<LoopId> path;
    for (std::uint32_t l = 0; l < loop_depths_.size(); ++l) {
      std::optional<LoopId> cursor{LoopId{l}};
      while (cursor) {
        quota_.Work();
        Require(cursor->value < loop_depths_.size());
        if (loop_depths_[cursor->value]) break;
        Require(path.size() < loop_depths_.size());
        Append(path, *cursor);
        cursor = control_.skeleton.loops[cursor->value].parent;
      }
      auto depth = cursor ? loop_depths_[cursor->value] : 0;
      while (!path.empty()) {
        quota_.Work();
        loop_depths_[path.back().value] = ++depth;
        path.pop_back();
      }
    }
    if (!evaluation_.holes) return;
    struct Incoming {
      bool hole = false, invalid = false;
      std::optional<ValueId> caught;
    };
    std::vector<Incoming> inputs;
    std::vector<std::optional<TryId>> catches;
    std::vector<std::optional<OpId>> guards;
    std::vector<std::uint8_t> multiple, rethrows;
    std::vector<std::uint8_t> candidates;
    quota_.Size(inputs, graph_.values.size());
    quota_.Size(catches, graph_.values.size());
    quota_.Size(guards, graph_.ops.size());
    quota_.Size(multiple, graph_.ops.size());
    quota_.Size(rethrows, graph_.ops.size());
    quota_.Size(candidates, exceptions_.regions.size());
    for (std::uint32_t r = 0; r < exceptions_.regions.size(); ++r) {
      quota_.Work();
      if (const auto binding = exceptions_.regions[r].binding)
        catches[binding->value.value] = TryId{r};
    }
    for (const auto& use : graph_.uses) {
      quota_.Work();
      const auto* target = std::get_if<ValueId>(&use.owner);
      if (!target || use.life != ValueLife::kLive) continue;
      auto& input = inputs[target->value];
      const auto* raw = std::get_if<ValueId>(&use.operand);
      const auto* value =
          raw ? std::get_if<ValueId>(&*canonical_[raw->value]) : nullptr;
      if (!value) {
        input.invalid = true;
        continue;
      }
      if (graph_.values[value->value].domain == ValueKinds::Hole())
        input.hole = true;
      else if (catches[value->value] &&
               (!input.caught || input.caught == *value))
        input.caught = *value;
      else
        input.invalid = true;
    }
    for (std::uint32_t o = 0; o < graph_.ops.size(); ++o) {
      quota_.Work();
      const auto& op = graph_.ops[o];
      if (!std::holds_alternative<ValueBranch>(op.operation)) continue;
      const auto condition = Operand({o}, 0);
      const auto* value = std::get_if<ValueId>(&condition);
      const auto* result =
          value ? std::get_if<OpResult>(&graph_.values[value->value].origin)
                : nullptr;
      if (!result) continue;
      if (guards[result->op.value]) multiple[result->op.value] = 1;
      guards[result->op.value] = OpId{o};
    }
    for (const auto& rethrow : evaluation_.holes->rethrows)
      rethrows[rethrow.comparison.value] = 1;
    for (const auto& comparison : evaluation_.holes->comparisons) {
      quota_.Work();
      const auto o = comparison.operation.value;
      const auto& input = inputs[comparison.payload.value];
      const auto* phi = std::get_if<PhiDefinition>(
          &graph_.values[comparison.payload.value].origin);
      if (!phi || !input.hole || !input.caught || input.invalid || !guards[o] ||
          multiple[o] || !rethrows[o])
        continue;
      const auto region = *catches[input.caught->value];
      if (control_.regions[region.value].exit != phi->block) continue;
      // Multiple tests are still printable, but do not choose a finalizer
      // boundary arbitrarily.
      if (candidates[region.value]) {
        shared_guards_[region.value].reset();
        continue;
      }
      candidates[region.value] = 1;
      shared_guards_[region.value] = guards[o];
    }
  }
  void Merge(SharedCompletion& result, const SharedCompletion& other) {
    result.valid &= other.valid;
    if (result.handler && other.handler && result.handler != other.handler) result.valid = false;
    if (other.handler) result.handler = other.handler;
    result.uncaught |= other.uncaught;
    if (result.uncaught && result.handler) result.valid = false;
    // After internal loops are discharged, valid transfers from this lexical
    // scope target its active ancestor-loop chain (checked by common control).
    // Retaining its outermost target represents all required enclosing loops
    // without copying a destination set through every nested try.
    if (other.loop && (!result.loop || loop_depths_[other.loop->value] < loop_depths_[result.loop->value]))
      result.loop = other.loop;
  }
  SharedCompletion SharedScope(TryId region, ControlId entry, bool handler) {
    struct Task {
      ControlId entry;
      std::optional<ControlId> stop;
    };
    std::vector<Task> pending;
    Append(pending, Task{entry, {}});
    const auto binding = exceptions_.regions[region.value].binding;
    if (!binding) return {false};
    SharedCompletion result;
    const auto consume = [&](SharedCompletion completion) {
      // A nested rethrow may enter this region's catch. That catch is
      // summarized separately, not mistaken for an escaping completion.
      if (!handler && completion.handler == binding->block) completion.handler.reset();
      if (completion.loop && shared_loops_[completion.loop->value] == entry) completion.loop.reset();
      Merge(result, completion);
    };
    while (!pending.empty()) {
      quota_.Work();
      const auto task = pending.back();
      pending.pop_back();
      if (task.stop == task.entry) continue;
      const auto id = task.entry;
      if (id.value >= control_.skeleton.nodes.size() || shared_visits_[id.value])
        return {false};
      shared_visits_[id.value] = 1;
      const auto& node = control_.skeleton.nodes[id.value];
      if (const auto* wrapper = std::get_if<LoopControl>(&node.synthetic)) {
        const auto& loop = control_.skeleton.loops[wrapper->loop.value];
        // The common loop forest has already bound transfers. Only loops
        // entered by this exact protected/handler scope are internal exits;
        // a jump to an enclosing loop still prevents shared-finally folding.
        shared_loops_[wrapper->loop.value] = entry;
        if (node.next) Append(pending, Task{*node.next, task.stop});
        Append(pending, Task{loop.body, {}});
        continue;
      }
      if (const auto* transfer = std::get_if<LoopTransfer>(&node.synthetic)) {
        consume({true, false, {}, transfer->loop});
        continue;
      }
      if (id.value >= control_.units.size()) return {false};
      if (const auto* exit =
              std::get_if<TryExitUnit>(&control_.units[id.value])) {
        if (exit->region != region) return {false};
        if (node.next) {
          const auto& route = control_.skeleton.nodes[node.next->value];
          if (!std::holds_alternative<LoopTransfer>(route.synthetic)) return {false};
          Append(pending, Task{*node.next, task.stop});
        }
        continue;
      }
      if (const auto* nested = std::get_if<TryControlUnit>(&control_.units[id.value])) {
        Require(regions_[nested->region.value].ready);
        consume(shared_completions_[nested->region.value]);
        if (!result.valid) return result;
        if (node.next) Append(pending, Task{*node.next, task.stop});
        else if (control_.regions[nested->region.value].exit && !task.stop) return {false};
        continue;
      }
      const auto* phase =
          std::get_if<PhaseControlUnit>(&control_.units[id.value]);
      Require(phase != nullptr);
      const auto& block = graph_.blocks[phase->block.value];
      bool caught_throw = false;
      for (std::uint32_t o = 0; o < block.op_count; ++o) {
        quota_.Work();
        const auto index = block.first_op.value + o;
        const auto& op = graph_.ops[index];
        // A yield's native RETURN is not a normal/throw edge in this graph.
        // Until its raw RETURN cleanup is recovered, moving following cleanup
        // into finally would add effects on a route that originally bypassed it.
        if (std::holds_alternative<ValueReturn>(op.operation) ||
            std::holds_alternative<ValueYield>(op.operation)) return {false};
        if (exits_[index]) {
          const auto& exit = graph_.exceptional_exits[*exits_[index]];
          consume({true, !exit.handler, exit.handler ? std::optional{exit.handler->entry} : std::nullopt});
        }
        caught_throw |= std::holds_alternative<ValueThrow>(op.operation);
      }
      if (node.next) Append(pending, Task{*node.next, task.stop});
      if (node.arms) {
        const auto join = node.next ? node.next : task.stop;
        if (node.arms->taken) Append(pending, Task{*node.arms->taken, join});
        if (node.arms->fallthrough)
          Append(pending, Task{*node.arms->fallthrough, join});
      }
      if (!node.next && !node.arms && !task.stop && !caught_throw) return {false};
    }
    return result;
  }
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
        if (std::holds_alternative<ValueReturn>(op.operation) || std::holds_alternative<ValueYield>(op.operation)) return false;
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
      if (id.value < control_.skeleton.nodes.size() &&
          std::holds_alternative<LoopTransfer>(control_.skeleton.nodes[id.value].synthetic)) {
        path.transfer = id;
        return true;
      }
      if (id.value >= control_.units.size()) return false;
      const auto* phase = std::get_if<PhaseControlUnit>(&control_.units[id.value]);
      const auto& node = control_.skeleton.nodes[id.value];
      // A routed try exit carries no value/effect operation. Follow only its
      // checked direct loop-transfer successor, retaining both control IDs.
      if (std::holds_alternative<TryExitUnit>(control_.units[id.value]) && node.next &&
          incoming_[id.value] == 1 && !node.arms &&
          std::holds_alternative<LoopTransfer>(control_.skeleton.nodes[node.next->value].synthetic)) {
        Append(path.nodes, id); path.transfer = *node.next; return true;
      }
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
            kind == EvaluationKind::kRemoved || std::holds_alternative<ValueJump>(graph_.ops[op.value].operation)) continue;
        // Materialized literal copies are real cleanup roots (e.g. the value
        // for `return undefined`). Keep them paired, not orphaned before the
        // finalizer. SSA/catch snapshots retain their existing separate route.
        if (std::holds_alternative<ValueCopy>(graph_.ops[op.value].operation) &&
            std::holds_alternative<ValueId>(Operand(op, 0))) continue;
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
    if (!supported || !SameCleanupOperation(left.operation, right.operation) ||
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
  std::vector<std::optional<OpId>> shared_guards_;
  std::vector<std::uint8_t> shared_visits_;
  std::vector<std::optional<ControlId>> shared_loops_;
  std::vector<std::size_t> loop_depths_;
  std::vector<SharedCompletion> shared_completions_;
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
