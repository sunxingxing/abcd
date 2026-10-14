#include "for_in_events.h"
#include "for_in_quota.h"
#include <new>
#include <stdexcept>
namespace abcd::source {
namespace {
using namespace analysis;
using Error = ForInError;
// Dense indexes are allocated through the quota before use. Each original
// phase and arc is visited once after the shared protocol proof. No source
// expression is assigned to either the advance result or the key commit.
class Events {
 public:
  Events(const ValueProgram& graph, detail::ForInQuota& quota)
      : g(graph), q(quota), exits(&q.memory), commits(&q.memory) {
    q.Require(g.exceptional_exits.size() <= UINT32_MAX && g.phases.size() <= UINT32_MAX, Error::kResourceLimit);
    q.Step(g.ops.size()); q.Step(g.ops.size());
    exits.resize(g.ops.size()); commits.resize(g.ops.size());
    for (std::uint32_t i = 0; i < g.exceptional_exits.size(); ++i) {
      q.Step(); const auto op = g.exceptional_exits[i].op;
      q.Require(op.value < exits.size() && !exits[op.value], Error::kExceptionCorrespondence);
      exits[op.value] = i;
    }
    q.Require(g.phases.empty() || g.phases.size() == g.blocks.size());
    for (std::uint32_t b = 0; b < g.phases.size(); ++b) {
      q.Step(); const auto& site = g.phases[b];
      if (site.phase != ExceptionPhase::kCommit || !site.operation) continue;
      q.Require(site.operation->value < commits.size() && !commits[site.operation->value]);
      commits[site.operation->value] = WorkBlockId{b};
    }
  }
  ForInEvent Event(OpId id, EnumerationAction action) {
    q.Step(); q.witness.operation = id;
    q.Require(id.value < g.ops.size());
    const auto& op = g.ops[id.value];
    const auto* enumeration = std::get_if<ValueEnumeration>(&op.operation);
    q.Require(enumeration && enumeration->action == action && op.result &&
        op.result->value < g.values.size() && op.life == ValueLife::kLive, Error::kIdentity);
    const auto* result = std::get_if<OpResult>(&g.values[op.result->value].origin);
    q.Require(result && result->op == id, Error::kIdentity);
    q.Require(exits[id.value].has_value(), Error::kExceptionCorrespondence);
    const auto& exit = g.exceptional_exits[*exits[id.value]];
    q.Require(exit.origin == op.origin && exit.before_throw == op.before_throw &&
        op.exceptional == ExceptionalCompletion::kMayThrow, Error::kExceptionCorrespondence);
    auto commit = op.block;
    if (!g.phases.empty()) {
      q.Require(op.block.value < g.phases.size() && commits[id.value]);
      const auto& before = g.phases[op.block.value];
      q.Require(before.phase == ExceptionPhase::kBefore && before.operation == id &&
          result->commit == commits[id.value]);
      commit = *commits[id.value];
      const auto& block = g.blocks[op.block.value];
      bool normal = false, raised = false;
      for (std::uint32_t n = 0; n < block.edge_count; ++n) {
        q.Step(); const EdgeId edge{block.first_edge.value + n};
        q.Require(edge.value < g.edges.size()); const auto& arc = g.edges[edge.value];
        if (arc.kind == FlowEdgeKind::kException) {
          q.Require(!raised && exit.handler && arc.target == exit.handler->entry,
              Error::kExceptionCorrespondence); raised = true;
        } else {
          q.Require(!normal && arc.kind == FlowEdgeKind::kFallthrough && arc.target == commit);
          normal = true;
        }
      }
      q.Require(normal && raised == exit.handler.has_value(), Error::kExceptionCorrespondence);
    } else q.Require(!result->commit);
    return {id, op.block, commit, *op.result, {*exits[id.value]}};
  }
  ForInEventPair Pair(const ForInLoop& loop) {
    q.witness = {Error::kCorrespondence, loop.acquisition};
    const auto acquire = Event(loop.acquisition, EnumerationAction::kAcquire);
    const auto advance = Event(loop.advance, EnumerationAction::kNext);
    q.Require(acquire.result == loop.enumerator && advance.result == loop.key, Error::kIdentity);
    q.witness.operation = loop.decision;
    q.Require(loop.decision.value < g.ops.size() && loop.done_edge != loop.body_edge);
    const auto& branch = g.ops[loop.decision.value];
    const auto owner = commits[loop.decision.value].value_or(branch.block);
    for (const auto edge : {loop.done_edge, loop.body_edge}) {
      q.Step(); q.witness.edge = edge;
      q.Require(edge.value < g.edges.size(), Error::kDone);
      const auto& arc = g.edges[edge.value];
      q.Require(arc.source == owner &&
          (arc.kind == FlowEdgeKind::kTaken || arc.kind == FlowEdgeKind::kFallthrough), Error::kDone);
      q.Require(arc.target == (edge == loop.done_edge ? loop.exit : loop.body), Error::kDone);
    }
    return {acquire, advance, {loop.decision, loop.body_edge, loop.done_edge, advance.result}};
  }
 private:
  const ValueProgram& g;
  detail::ForInQuota& q;
  std::pmr::vector<std::optional<std::uint32_t>> exits;
  std::pmr::vector<std::optional<WorkBlockId>> commits;
};
void Protocol(const ValueProgram& graph, const ForInPlan& plan, detail::ForInQuota& q) {
  const auto built = BuildForInPlan(graph, q.Remaining());
  if (const auto* error = std::get_if<ForInFailure>(&built)) throw *error;
  const auto& checked = std::get<ForInPlan>(built);
  q.memory.Observe(checked.allocated_bytes); q.Step(checked.work);
  q.Step(plan.loops.size() + plan.keys.size() + plan.forwarding.size());
  q.Require(plan.revision == checked.revision && plan.cfg_revision == checked.cfg_revision &&
      plan.loops == checked.loops && plan.keys == checked.keys && plan.forwarding == checked.forwarding);
}
} // namespace
std::variant<ForInEvents, ForInFailure> BuildForInEvents(
    const ValueProgram& graph, const ForInPlan& plan, SemanticBudget budget) {
  detail::ForInQuota q(budget);
  try {
    Protocol(graph, plan, q); Events events(graph, q);
    ForInEvents result{graph.revision, graph.cfg_revision};
    q.Reserve(result.loops, plan.loops.size());
    for (const auto& loop : plan.loops) { q.Step(); result.loops.push_back(events.Pair(loop)); }
    result.allocated_bytes = q.memory.Peak(); result.work = q.initial - q.remaining;
    return result;
  } catch (ForInFailure failure) { return failure;
  } catch (const std::bad_alloc&) { q.witness.kind = Error::kResourceLimit; return q.witness;
  } catch (const std::length_error&) { q.witness.kind = Error::kResourceLimit; return q.witness; }
}
std::variant<std::uint64_t, ForInFailure> VerifyForInEvents(
    const ValueProgram& graph, const ForInPlan& plan, const ForInEvents& proof, SemanticBudget budget) {
  detail::ForInQuota q(budget);
  try {
    q.Require(proof.revision == graph.revision && proof.cfg_revision == graph.cfg_revision &&
        proof.loops.size() == plan.loops.size());
    Protocol(graph, plan, q); Events events(graph, q);
    for (std::size_t i = 0; i < plan.loops.size(); ++i) {
      q.Step(); q.Require(events.Pair(plan.loops[i]) == proof.loops[i]);
    }
    return q.initial - q.remaining;
  } catch (ForInFailure failure) { return failure;
  } catch (const std::bad_alloc&) { q.witness.kind = Error::kResourceLimit; return q.witness;
  } catch (const std::length_error&) { q.witness.kind = Error::kResourceLimit; return q.witness; }
}
} // namespace abcd::source
