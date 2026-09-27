#include "phase_reference.h"

#include <new>
#include <stdexcept>
#include <type_traits>
#include "../analysis/value_edits.h"

namespace abcd::source {
namespace {
using namespace analysis;
using Error = EvaluationError;
void Require(bool condition, Error error) { if (!condition) throw error; }
struct Coordinate {
  std::optional<WorkBlockId> head;
  std::size_t order = 0, fences = 0, copies = 0;
};
struct Occurrence { std::optional<UseId> first; bool multiple = false; };
bool SameOperand(const ValueOperand& a, const ValueOperand& b) {
  if (a.index() != b.index()) return false;
  return std::visit([&](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, SpecialValue>) return value == std::get<T>(b);
    else return value.value == std::get<T>(b).value;
  }, a);
}
}  // namespace
bool CrossPhaseReference(const ValueProgram& graph, const EvaluationPlan& plan, OpId property) {
  const auto& placement = plan.operations[property.value];
  if (plan.flow != EvaluationFlow::kPhased || placement.kind != EvaluationKind::kInline ||
      !placement.consumer) return false;
  const auto* owner = std::get_if<OpId>(&graph.uses[placement.consumer->value].owner);
  return owner && graph.ops[owner->value].block != graph.ops[property.value].block;
}
std::variant<PhaseReferences, EvaluationError> CheckPhaseReferences(
    const ValueProgram& graph, const EvaluationPlan& plan, SemanticBudget allowance) {
  try {
    PhaseReferences result;
    if (plan.flow != EvaluationFlow::kPhased) return result;
    auto bytes = allowance.bytes;
    auto work = allowance.work;
    const auto step = [&](std::size_t count = 1) {
      Require(count <= work, Error::kWorkLimit); work -= count;
    };
    const auto array = [&]<class T>(std::vector<T>& values, std::size_t count) {
      Require(count <= bytes / sizeof(T), Error::kResourceLimit);
      step(count); values.resize(count);
      Require(values.capacity() <= bytes / sizeof(T), Error::kResourceLimit);
      bytes -= values.capacity() * sizeof(T);
    };
    bool calls = false;
    for (const auto& op : graph.ops) {
      step(); const auto* invoke = std::get_if<ValueInvoke>(&op.operation);
      calls |= op.life == ValueLife::kLive && invoke && invoke->receiver != InvokeReceiver::kUndefined;
    }
    if (!calls) { result.work = allowance.work - work; return result; }
    std::vector<std::size_t> incoming;
    std::vector<std::optional<EdgeId>> next;
    std::vector<std::uint8_t> previous, copied;
    std::vector<Coordinate> positions;
    std::vector<Occurrence> uses;
    std::vector<std::optional<std::size_t>> exits, throws, staged;
    array(result.consumers, graph.ops.size());
    array(incoming, graph.blocks.size()); array(next, graph.blocks.size());
    array(previous, graph.blocks.size()); array(positions, graph.blocks.size());
    array(copied, graph.edges.size()); array(uses, graph.values.size());
    array(exits, graph.ops.size()); array(throws, graph.ops.size());
    array(staged, plan.exceptions ? plan.exceptions->slots.size() : 0);
    for (const auto& edge : graph.edges) { step(); ++incoming[edge.target.value]; }
    for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
      step(); const auto& block = graph.blocks[b];
      std::optional<EdgeId> edge;
      std::size_t count = 0;
      for (std::size_t e = block.first_edge.value; e < std::size_t(block.first_edge.value) + block.edge_count; ++e) {
        step(); if (graph.edges[e].kind == FlowEdgeKind::kException) continue;
        ++count; edge = EdgeId{static_cast<std::uint32_t>(e)};
      }
      if (count != 1 || graph.phases[b].phase == ExceptionPhase::kCatch) continue;
      const auto& flow = graph.edges[edge->value];
      if (flow.kind != FlowEdgeKind::kFallthrough || incoming[flow.target.value] != 1 ||
          graph.phases[flow.target.value].phase == ExceptionPhase::kCatch) continue;
      next[b] = edge; previous[flow.target.value] = 1;
    }
    if (plan.exceptions) {
      for (std::size_t b = 0; b < plan.phis->bundles.size(); ++b) {
        step(); const auto& placement = plan.exceptions->placements[b];
        if (const auto* normal = std::get_if<NormalCopies>(&placement))
          copied[normal->edge.value] = plan.phis->bundles[b].count != 0;
        if (const auto* raised = std::get_if<ThrowCopies>(&placement)) throws[raised->operation.value] = b;
      }
    }
    // Each block and edge is visited once. Cyclic components without a head
    // remain unindexed: this certificate never authorizes a loop crossing.
    for (std::size_t root = 0; root < graph.blocks.size(); ++root) {
      step(); if (previous[root]) continue;
      Coordinate pos{WorkBlockId{static_cast<std::uint32_t>(root)}};
      auto b = root;
      for (;;) {
        step(); Require(!positions[b].head, Error::kInvalidGraph);
        positions[b] = pos;
        const auto& block = graph.blocks[b];
        for (std::size_t o = block.first_op.value; o < std::size_t(block.first_op.value) + block.op_count; ++o) {
          step(); const auto& op = graph.ops[o];
          if (op.life == ValueLife::kLive && !CanErasePureOp(op)) ++pos.fences;
        }
        if (!next[b]) break;
        pos.copies += copied[next[b]->value]; ++pos.order;
        b = graph.edges[next[b]->value].target.value;
      }
    }
    for (std::size_t i = 0; i < graph.exceptional_exits.size(); ++i) {
      step(); exits[graph.exceptional_exits[i].op.value] = i;
    }
    for (std::size_t i = 0; i < graph.uses.size(); ++i) {
      step(); const auto& use = graph.uses[i];
      if (const auto* owner = std::get_if<OpId>(&use.owner)) {
        const auto kind = plan.operations[owner->value].kind;
        if (graph.ops[owner->value].life != ValueLife::kLive ||
            kind == EvaluationKind::kAlias || kind == EvaluationKind::kElidedPure) continue;
      }
      const auto operand = ResolveEvaluationOperand(plan, use.operand);
      const auto* value = std::get_if<ValueId>(&operand);
      if (!value) continue;
      auto& occurrence = uses[value->value];
      if (occurrence.first) occurrence.multiple = true;
      else occurrence.first = UseId{static_cast<std::uint32_t>(i)};
    }
    const auto count = [&](std::optional<std::size_t> bundle) {
      return bundle ? plan.phis->bundles[*bundle].count : 0U;
    };
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step(); const auto& call = graph.ops[i];
      const auto* invoke = std::get_if<ValueInvoke>(&call.operation);
      if (call.life != ValueLife::kLive || !invoke || invoke->receiver == InvokeReceiver::kUndefined) continue;
      const auto shape = DescribeEvaluation(call.operation);
      Require(shape && shape->Count() == call.use_count, Error::kInvalidGraph);
      const auto callee = ResolveEvaluationOperand(plan, graph.uses[call.first_use.value].operand);
      const auto* value = std::get_if<ValueId>(&callee);
      if (!value || uses[value->value].multiple || uses[value->value].first != call.first_use) continue;
      const auto* producer = std::get_if<OpResult>(&graph.values[value->value].origin);
      if (!producer) continue;
      const auto id = producer->op;
      const auto& property = graph.ops[id.value];
      if (property.life != ValueLife::kLive ||
          (!std::holds_alternative<ValueNamedRead>(property.operation) &&
           !std::holds_alternative<ValuePropertyRead>(property.operation))) continue;
      const auto before = property.block, after = call.block;
      const auto& start = positions[before.value];
      const auto& end = positions[after.value];
      if (!start.head || start.head != end.head || start.order >= end.order ||
          end.fences != start.fences + 1 || start.copies != end.copies ||
          graph.blocks[before.value].op_count != 1 || graph.blocks[after.value].op_count != 1) continue;
      if (!exits[id.value] || !exits[i] || graph.exceptional_exits[*exits[id.value]].handler !=
          graph.exceptional_exits[*exits[i]].handler) continue;
      const auto left = throws[id.value], right = throws[i];
      if (count(left) != count(right)) continue;
      bool same = true;
      if (count(left)) {
        const auto& a = plan.phis->bundles[*left];
        const auto& b = plan.phis->bundles[*right];
        for (std::size_t c = a.first; c < std::size_t(a.first) + a.count; ++c) {
          step(); staged[plan.exceptions->staging[c]->value] = c;
        }
        for (std::size_t c = b.first; c < std::size_t(b.first) + b.count; ++c) {
          step(); const auto prior = staged[plan.exceptions->staging[c]->value];
          same &= prior && *prior >= a.first && *prior < std::size_t(a.first) + a.count &&
              SameOperand(ResolveEvaluationOperand(plan, plan.phis->copies[*prior].source),
                          ResolveEvaluationOperand(plan, plan.phis->copies[c].source));
        }
      }
      if (same) result.consumers[id.value] = call.first_use;
    }
    result.bytes = allowance.bytes - bytes;
    result.work = allowance.work - work;
    return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
