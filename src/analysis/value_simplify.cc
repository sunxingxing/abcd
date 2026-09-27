#include "abcd/analysis/value_simplify.h"

#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

#include "value_edits.h"
#include "value_flow.h"
#include "value_phi.h"
#include "value_rewrite.h"

namespace abcd::analysis {
namespace {
using Error = ValueSimplifyError;
void Require(bool condition, Error error) {
  if (!condition) throw error;
}
class BatchBudget {
 public:
  explicit BatchBudget(SemanticBudget budget)
      : bytes_(budget.bytes), work_(budget.work) {}
  void Work(std::size_t count = 1) {
    Require(count <= work_, Error::kWorkLimit);
    work_ -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= bytes_ / width, Error::kResourceLimit);
    bytes_ -= count * width;
  }
  SemanticBudget Remaining() const { return {bytes_, work_}; }
  template <typename T> void Reserve(std::vector<T>& items, std::size_t count) {
    Require(count <= std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
    Admit(count, sizeof(T));
    items.reserve(count);
    if (items.capacity() > count) Admit(items.capacity() - count, sizeof(T));
  }
  void CheckUses(const ValueProgram& graph) {
    Work(graph.values.size());
    Work(graph.ops.size());
    // Linked traversal, owner ranges and occurrence census are separate passes.
    Work(graph.uses.size());
    Work(graph.uses.size());
    Work(graph.uses.size());
    if (const auto error = VerifyValueUses(graph, graph.uses.size()))
      throw *error == ValueGraphError::kResourceLimit
          ? Error::kResourceLimit : Error::kInvalidGraph;
  }
  void CheckFlow(const ValueProgram& graph) {
    const auto checked = VerifyValueFlow(graph, {bytes_, work_});
    if (const auto* proof = std::get_if<ValueFlowProof>(&checked)) {
      Work(proof->work);
      return;  // All verifier storage is released before returning.
    }
    switch (std::get<ValueAvailabilityError>(checked)) {
      case ValueAvailabilityError::kWorkLimit: throw Error::kWorkLimit;
      case ValueAvailabilityError::kResourceLimit: throw Error::kResourceLimit;
      case ValueAvailabilityError::kUnreachableBlock:
      case ValueAvailabilityError::kResumeFlow: throw Error::kUnsupportedFlow;
      case ValueAvailabilityError::kInvalidGraph:
      case ValueAvailabilityError::kInvalidTerminator:
      case ValueAvailabilityError::kInvalidPhi:
      case ValueAvailabilityError::kUnavailableValue: throw Error::kInvalidGraph;
    }
  }
 private:
  std::size_t bytes_;
  std::uint64_t work_;
};

template <typename Graph, typename F> void GraphArrays(Graph& graph, F visit) {
  visit(graph.blocks);
  visit(graph.edges);
  visit(graph.ops);
  visit(graph.values);
  visit(graph.uses);
  visit(graph.origins);
  visit(graph.exceptional_exits);
  visit(graph.input.sites);
  visit(graph.input.values);
}

ValueProgram Clone(const ValueProgram& input, BatchBudget& quota) {
  ValueProgram candidate;
  candidate.revision = input.revision;
  candidate.cfg_revision = input.cfg_revision;
  candidate.arguments = input.arguments;
  const auto copy = [&](const auto& from, auto& to) {
    quota.Reserve(to, from.size());
    quota.Work(from.size());
    to.assign(from.begin(), from.end());
  };
  copy(input.blocks, candidate.blocks);
  copy(input.edges, candidate.edges);
  copy(input.ops, candidate.ops);
  copy(input.values, candidate.values);
  copy(input.uses, candidate.uses);
  copy(input.origins, candidate.origins);
  copy(input.exceptional_exits, candidate.exceptional_exits);
  copy(input.input.sites, candidate.input.sites);
  copy(input.input.values, candidate.input.values);
  return candidate;
}

enum class AliasVisit : std::uint8_t { kUnseen, kActive, kResolved };
struct BatchScratch {
  std::vector<ValueId> canonical;
  std::vector<AliasVisit> visit;
  std::vector<ValueId> pending;
  std::vector<std::uint8_t> live_ops;
  std::vector<std::uint8_t> live_values;
  std::vector<std::optional<UseId>> phi_head;
  std::vector<std::optional<UseId>> phi_next;
  BatchScratch(const ValueProgram& graph, BatchBudget& quota) {
    const auto sized = [&](auto& items, std::size_t size) {
      quota.Reserve(items, size);
      quota.Work(size);
      items.resize(size);
    };
    sized(canonical, graph.values.size());
    sized(visit, graph.values.size());
    quota.Reserve(pending, graph.values.size());
    sized(live_ops, graph.ops.size());
    sized(live_values, graph.values.size());
    sized(phi_head, graph.values.size());
    sized(phi_next, graph.uses.size());
  }
};

void Canonicalize(ValueProgram& graph, BatchScratch& scratch,
                  BatchBudget& quota, ValueSimplifyStats& stats) {
  auto& aliases = scratch.canonical;
  for (std::size_t i = 0; i < aliases.size(); ++i) {
    quota.Work();
    aliases[i] = ValueId{static_cast<std::uint32_t>(i)};
  }
  for (const auto& op : graph.ops) {
    quota.Work();
    if (op.life != ValueLife::kLive || !std::holds_alternative<ValueCopy>(op.operation) ||
        !CanErasePureOp(op) || !op.result || op.use_count != 1) continue;
    const auto& use = graph.uses[op.first_use.value];
    const auto* source = std::get_if<ValueId>(&use.operand);
    if (!source || use.role != OperandRole::kInput ||
        use.purpose != ReadPurpose::kOperand) continue;
    Require(*source != *op.result, Error::kCopyCycle);
    // Do not broaden a refined input certificate by replacing it with a value
    // whose facts are weaker. Constants are not materialized as fake ValueIds.
    const auto domain = graph.values[source->value].domain;
    const auto admitted = graph.values[op.result->value].domain;
    if (domain.IsEmpty() || domain.Join(admitted) != admitted)
      continue;
    aliases[op.result->value] = *source;
  }
  // Functional-graph walk with path compression: each unresolved vertex is
  // entered once, including chains whose definition IDs are not topological.
  for (std::size_t i = 0; i < aliases.size(); ++i) {
    quota.Work();
    if (scratch.visit[i] == AliasVisit::kResolved) continue;
    scratch.pending.clear();
    ValueId cursor{static_cast<std::uint32_t>(i)};
    while (scratch.visit[cursor.value] == AliasVisit::kUnseen &&
           aliases[cursor.value] != cursor) {
      quota.Work();
      scratch.visit[cursor.value] = AliasVisit::kActive;
      scratch.pending.push_back(cursor);
      cursor = aliases[cursor.value];
    }
    Require(scratch.visit[cursor.value] != AliasVisit::kActive, Error::kCopyCycle);
    const auto root = aliases[cursor.value];
    scratch.visit[cursor.value] = AliasVisit::kResolved;
    for (const auto value : scratch.pending) {
      quota.Work();
      aliases[value.value] = root;
      scratch.visit[value.value] = AliasVisit::kResolved;
    }
  }
  for (std::size_t i = 0; i < graph.uses.size(); ++i) {
    quota.Work();
    const auto& use = graph.uses[i];
    const auto* value = std::get_if<ValueId>(&use.operand);
    if (use.life != ValueLife::kLive || !value || aliases[value->value] == *value)
      continue;
    Require(!ReplaceUse(graph, UseId{static_cast<std::uint32_t>(i)},
                        aliases[value->value]), Error::kInvalidGraph);
    ++stats.replaced_uses;
  }
}

void MarkLive(const ValueProgram& graph, BatchScratch& scratch, BatchBudget& quota) {
  scratch.pending.clear();
  for (std::size_t i = 0; i < graph.uses.size(); ++i) {
    quota.Work();
    const auto& use = graph.uses[i];
    const auto* phi = std::get_if<ValueId>(&use.owner);
    if (!phi || use.life != ValueLife::kLive) continue;
    scratch.phi_next[i] = scratch.phi_head[phi->value];
    scratch.phi_head[phi->value] = UseId{static_cast<std::uint32_t>(i)};
  }
  const auto mark_value = [&](const ValueOperand& operand) {
    const auto* value = std::get_if<ValueId>(&operand);
    if (!value || scratch.live_values[value->value]) return;
    scratch.live_values[value->value] = 1;
    scratch.pending.push_back(*value);
  };
  const auto mark_op = [&](OpId id) {
    if (scratch.live_ops[id.value]) return;
    scratch.live_ops[id.value] = 1;
    const auto& op = graph.ops[id.value];
    for (std::uint32_t ordinal = 0; ordinal < op.use_count; ++ordinal) {
      quota.Work();
      mark_value(graph.uses[op.first_use.value + ordinal].operand);
    }
  };
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    quota.Work();
    const auto& op = graph.ops[i];
    if (op.life == ValueLife::kLive && !CanErasePureOp(op))
      mark_op(OpId{static_cast<std::uint32_t>(i)});
  }
  for (std::size_t i = 0; i < scratch.pending.size(); ++i) {
    quota.Work();
    const auto id = scratch.pending[i];
    const auto& value = graph.values[id.value];
    if (const auto* result = std::get_if<OpResult>(&value.origin)) {
      mark_op(result->op);
    } else if (std::holds_alternative<PhiDefinition>(value.origin)) {
      for (auto use = scratch.phi_head[id.value]; use;
           use = scratch.phi_next[use->value]) {
        quota.Work();
        mark_value(graph.uses[use->value].operand);
      }
    }
  }
}
}  // namespace

std::variant<ValueSimplifyStats, ValueSimplifyError> SimplifyValues(
    ValueProgram& graph, IrRevision expected, SemanticBudget budget) {
  if (graph.revision != expected) return Error::kStaleRevision;
  try {
    BatchBudget quota{budget};
    quota.Admit(1, sizeof(ValueProgram));
    quota.Admit(1, sizeof(ValueProgram));
    GraphArrays(graph, [&](const auto& items) {
      using T = typename std::decay_t<decltype(items)>::value_type;
      quota.Admit(items.capacity(), sizeof(T));
    });
    // Admit the complete original and verifier before allocating a candidate.
    // Original capacities, not sizes, remain charged while both owners coexist.
    quota.Admit(graph.uses.size(), sizeof(std::uint8_t));
    auto candidate = Clone(graph, quota);
    quota.CheckUses(candidate);
    quota.CheckFlow(candidate);
    BatchScratch scratch{candidate, quota};
    ValueSimplifyStats stats{expected};
    Canonicalize(candidate, scratch, quota, stats);
    const auto phis = SimplifyPhis(candidate, quota.Remaining(), scratch.canonical);
    if (const auto* error = std::get_if<ValueSimplifyError>(&phis)) throw *error;
    const auto& collapsed = std::get<PhiSimplifyStats>(phis);
    quota.Work(collapsed.work);
    stats.replaced_uses += collapsed.replaced_uses;
    MarkLive(candidate, scratch, quota);
    for (std::size_t i = 0; i < candidate.ops.size(); ++i) {
      quota.Work();
      if (candidate.ops[i].life == ValueLife::kLive && !scratch.live_ops[i])
        ++stats.removed_ops;
    }
    for (std::size_t i = 0; i < candidate.values.size(); ++i) {
      quota.Work();
      const auto& value = candidate.values[i];
      if (value.life == ValueLife::kLive && !scratch.live_values[i] &&
          std::holds_alternative<PhiDefinition>(value.origin)) ++stats.removed_phis;
    }
    // Bound each pass inside EraseUnmarkedValues, including pure-op input
    // unlinking. This is a single batch, not repeated erase-to-fixpoint scans.
    quota.Work(candidate.ops.size());
    quota.Work(candidate.ops.size());
    quota.Work(candidate.values.size());
    quota.Work(candidate.uses.size());
    quota.Work(candidate.uses.size());
    quota.Work(candidate.uses.size());
    Require(!EraseUnmarkedValues(candidate, scratch.live_ops, scratch.live_values),
            Error::kInvalidGraph);
    quota.CheckUses(candidate);
    quota.CheckFlow(candidate);
    const auto rewritten = VerifyValueRewrite(graph, candidate, scratch.canonical, quota.Remaining());
    if (const auto* error = std::get_if<ValueRewriteError>(&rewritten)) {
      if (*error == ValueRewriteError::kWorkLimit) throw Error::kWorkLimit;
      if (*error == ValueRewriteError::kResourceLimit) throw Error::kResourceLimit;
      throw Error::kInvalidGraph;
    }
    quota.Work(std::get<ValueRewriteProof>(rewritten).work);
    if (stats.replaced_uses == 0 && stats.removed_ops == 0 && stats.removed_phis == 0)
      return stats;
    Require(expected.value != std::numeric_limits<std::uint64_t>::max(),
            Error::kRevisionLimit);
    candidate.revision.value = expected.value + 1;
    stats.revision = candidate.revision;
    static_assert(std::is_nothrow_move_assignable_v<ValueProgram>);
    graph = std::move(candidate);
    return stats;
  } catch (Error error) {
    return error;
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  } catch (const std::length_error&) {
    return Error::kResourceLimit;
  }
}
}  // namespace abcd::analysis
