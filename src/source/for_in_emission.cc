#include "for_in_emission.h"
#include "for_in_quota.h"
#include "evaluation_plan.h"
#include <new>
#include <stdexcept>
namespace abcd::source {
using namespace analysis;
namespace {
ForInEmissionView Build(const ValueProgram& g, const ExceptionPlan& regions,
    const ExceptionControl& control, const ForInPlan& plan, const ForInEvents& events,
    detail::ForInQuota& q) {
  const auto verified = VerifyForInEvents(g, plan, events, q.Remaining());
  if (const auto* error = std::get_if<ForInFailure>(&verified)) throw *error;
  q.Step(std::get<std::uint64_t>(verified));
  ForInEmissionView result;
  q.Reserve(result.loops, plan.loops.size());
  q.Reserve(result.continuations, control.skeleton.nodes.size()); result.continuations.resize(control.skeleton.nodes.size());
  q.Reserve(result.wrappers, control.skeleton.loops.size()); result.wrappers.resize(control.skeleton.loops.size());
  q.Reserve(result.forwarded_regions, regions.regions.size()); result.forwarded_regions.resize(regions.regions.size());
  q.Reserve(result.forwarded_operations, g.ops.size()); result.forwarded_operations.resize(g.ops.size());
  q.Reserve(result.forwarded_blocks, g.blocks.size()); result.forwarded_blocks.resize(g.blocks.size());
  std::pmr::vector<std::optional<ControlId>> nodes(g.blocks.size(), {}, &q.memory);
  std::pmr::vector<std::optional<LoopId>> headers(g.blocks.size(), {}, &q.memory);
  for (std::uint32_t n = 0; n < control.units.size(); ++n) {
    q.Step(); if (const auto* phase = std::get_if<PhaseControlUnit>(&control.units[n])) {
      q.Require(phase->block.value < nodes.size() && !nodes[phase->block.value]); nodes[phase->block.value] = ControlId{n};
    }
  }
  for (std::uint32_t l = 0; l < control.skeleton.loops.size(); ++l) {
    q.Step(); const auto h = control.skeleton.loops[l].header.value;
    q.Require(h < control.units.size(), ForInError::kExit);
    const auto* phase = std::get_if<PhaseControlUnit>(&control.units[h]);
    q.Require(phase && !headers[phase->block.value], ForInError::kExit);
    headers[phase->block.value] = LoopId{l};
  }
  auto summarized = AnalyzeExceptionForwarding(g, q.Remaining());
  if (const auto* error = std::get_if<ForInFailure>(&summarized)) throw *error;
  const auto& forwarding = std::get<ExceptionForwarding>(summarized);
  q.memory.Retain(forwarding.allocated_bytes); q.Step(forwarding.work);
  std::pmr::vector<std::optional<std::uint32_t>> acquisitions(g.ops.size(), {}, &q.memory);
  for (std::uint32_t i = 0; i < plan.loops.size(); ++i) {
    q.Step(); const auto& loop = plan.loops[i]; const auto& event = events.loops[i];
    q.witness = {ForInError::kCorrespondence, loop.acquisition};
    q.Require(headers[loop.header.value], ForInError::kExit);
    const auto id = *headers[loop.header.value];
    q.Require(!result.wrappers[id.value], ForInError::kExit); result.wrappers[id.value] = i;
    const auto branch_block = g.edges[loop.body_edge.value].source;
    q.Require(nodes[branch_block.value], ForInError::kDone);
    const auto decision = *nodes[branch_block.value]; const auto& node = control.skeleton.nodes[decision.value];
    q.Require(node.arms && !node.decision, ForInError::kDone);
    const auto arm = g.edges[loop.body_edge.value].kind == FlowEdgeKind::kTaken ? node.arms->taken : node.arms->fallthrough;
    // An empty selected arm continues at the already-verified join. This is
    // ControlPlan's edge interpretation, not a new follow-selection rule.
    const auto body = arm ? arm : node.next;
    q.Require(body, ForInError::kExit);
    EnumerationException exception = PhysicalCatch{event.acquisition.exceptional_exit};
    const auto& exit = g.exceptional_exits[event.acquisition.exceptional_exit.value];
    if (exit.handler) {
      const auto h = exit.handler->entry;
      q.Require(h.value < forwarding.blocks.size() && forwarding.blocks[h.value], ForInError::kExceptionCorrespondence);
      const auto& f = *forwarding.blocks[h.value];
      q.Require(!f.destination, ForInError::kCatchState);
      exception = ForwardedExceptionRoute{event.acquisition.exceptional_exit, h, f.payload, f.destination};
      acquisitions[loop.acquisition.value] = i;
    }
    result.loops.push_back({id, decision, body, exception});
  }
  for (const auto& node : control.skeleton.nodes) {
    q.Step(); const auto* transfer = std::get_if<LoopTransfer>(&node.synthetic);
    if (!transfer || transfer->kind != LoopTransferKind::kBreak || !result.wrappers[transfer->loop.value]) continue;
    const auto i = *result.wrappers[transfer->loop.value];
    const auto* source = std::get_if<EdgeId>(&transfer->source);
    if (!source || source->value >= control.arcs.size() || control.arcs[source->value].edge != plan.loops[i].done_edge)
      result.loops[i].exhaustion_only = false;
  }
  // Protected scopes are indexed by instruction. A mixed protected region is
  // not erased: each non-protocol throw entry would need its own source catch.
  // This first adapter refuses that staging, rather than forwarding it too.
  for (const auto& exit : g.exceptional_exits) {
    q.Step(); if (!exit.handler) continue;
    const auto op = exit.op; const auto site = g.phases[g.ops[op.value].block.value].instruction.value;
    q.Require(site < regions.layout.scopes.size()); const auto scope = regions.layout.scopes[site];
    if (acquisitions[op.value]) {
      q.Require(scope && scope->part == CatchPart::kProtected, ForInError::kCatchState);
      const auto i = *acquisitions[op.value]; auto& owner = result.forwarded_regions[scope->region.value];
      q.Require(!owner || *owner == i, ForInError::kCatchState); owner = i;
    }
  }
  for (const auto& exit : g.exceptional_exits) {
    q.Step(); if (!exit.handler) continue;
    const auto site = g.phases[g.ops[exit.op.value].block.value].instruction.value;
    const auto scope = regions.layout.scopes[site];
    if (scope && result.forwarded_regions[scope->region.value]) {
      q.witness.operation = exit.op;
      q.Require(acquisitions[exit.op.value] == result.forwarded_regions[scope->region.value], ForInError::kCatchState);
    }
  }
  // Each physical handler entry is visited once. Shared entries must agree on
  // payload/destination; every incoming exception edge is checked below.
  std::pmr::vector<WorkBlockId> pending(&q.memory); pending.reserve(g.blocks.size());
  std::pmr::vector<std::uint8_t> queued(g.blocks.size(), 0, &q.memory);
  std::pmr::vector<std::uint8_t> chains(control.skeleton.nodes.size(), 0, &q.memory);
  for (std::uint32_t r = 0; r < result.forwarded_regions.size(); ++r) {
    q.Step(); if (!result.forwarded_regions[r]) continue;
    const auto i = *result.forwarded_regions[r]; const auto& route = std::get<ForwardedExceptionRoute>(result.loops[i].acquisition);
    const auto& region = control.regions[r];
    result.continuations[region.wrapper.value] = EnumerationContinuation{region.body};
    std::optional<ControlId> at = region.body;
    while (at) {
      q.Step(); q.Require(!chains[at->value], ForInError::kExit); chains[at->value] = 1;
      const auto& node = control.skeleton.nodes[at->value];
      if (!node.next) result.continuations[at->value] = EnumerationContinuation{control.skeleton.nodes[region.wrapper.value].next};
      at = node.next;
    }
    if (!queued[route.handler.value]) { queued[route.handler.value] = 1; pending.push_back(route.handler); }
    while (!pending.empty()) {
      q.Step(); const auto b = pending.back(); pending.pop_back();
      if (result.forwarded_blocks[b.value]) continue;
      q.Require(forwarding.blocks[b.value] && forwarding.blocks[b.value]->payload == route.payload &&
          forwarding.blocks[b.value]->destination == route.destination, ForInError::kExceptionCorrespondence);
      result.forwarded_blocks[b.value] = i;
      const auto& block = g.blocks[b.value];
      for (std::uint32_t o = 0; o < block.op_count; ++o) { q.Step(); result.forwarded_operations[block.first_op.value + o] = i; }
      for (std::uint32_t e = 0; e < block.edge_count; ++e) {
        q.Step(); const auto& edge = g.edges[block.first_edge.value + e];
        q.Require(edge.kind != FlowEdgeKind::kException, ForInError::kExceptionCorrespondence);
        if (!queued[edge.target.value]) { queued[edge.target.value] = 1; pending.push_back(edge.target); }
      }
    }
  }
  for (const auto& edge : g.edges) {
    q.Step(); if (!result.forwarded_blocks[edge.target.value] || result.forwarded_blocks[edge.source.value]) continue;
    q.Require(edge.kind == FlowEdgeKind::kException, ForInError::kExceptionCorrespondence);
    const auto& phase = g.phases[edge.source.value];
    q.Require(phase.operation && acquisitions[phase.operation->value], ForInError::kCatchState);
  }
  result.allocated_bytes = q.memory.Peak(); result.work = q.initial - q.remaining;
  return result;
}
}
std::variant<ForInEmissionView, ForInFailure> BuildForInEmissionView(const ValueProgram& g,
    const ExceptionPlan& regions, const ExceptionControl& control, const ForInPlan& plan,
    const ForInEvents& events, SemanticBudget budget) {
  detail::ForInQuota q(budget);
  try { return Build(g, regions, control, plan, events, q); }
  catch (ForInFailure error) { return error; }
  catch (const std::bad_alloc&) { q.witness.kind = ForInError::kResourceLimit; return q.witness; }
  catch (const std::length_error&) { q.witness.kind = ForInError::kResourceLimit; return q.witness; }
}
std::variant<std::uint64_t, ForInFailure> VerifyForInCopyState(const ValueProgram& g,
    const EvaluationPlan& evaluation, const ForInEmissionView& view, SemanticBudget budget) {
  detail::ForInQuota q(budget);
  try {
    q.Require(evaluation.for_in && view.loops.size() == evaluation.for_in->loops.size());
    std::pmr::vector<std::optional<std::uint32_t>> done(g.edges.size(), {}, &q.memory);
    for (std::uint32_t l = 0; l < view.loops.size(); ++l) {
      q.Step(); done[evaluation.for_in->loops[l].done_edge.value] = l;
    }
    if (evaluation.phis) for (std::size_t b = 0; b < evaluation.phis->bundles.size(); ++b) {
      q.Step(); const auto& bundle = evaluation.phis->bundles[b];
      const auto* edge = std::get_if<EdgeId>(&bundle.incoming); if (!edge) continue;
      q.witness.edge = *edge;
      if (done[edge->value] && !view.loops[*done[edge->value]].exhaustion_only) {
        q.witness.acquisition = evaluation.for_in->loops[*done[edge->value]].acquisition;
        q.Require(bundle.schedule.steps.empty(), ForInError::kExit);
      }
      const auto& original = g.edges[edge->value];
      if (view.forwarded_blocks[original.source.value] || view.forwarded_blocks[original.target.value]) {
        const auto l = view.forwarded_blocks[original.source.value] ?
            *view.forwarded_blocks[original.source.value] : *view.forwarded_blocks[original.target.value];
        q.witness.acquisition = evaluation.for_in->loops[l].acquisition;
        if (evaluation.exceptions) if (const auto* caught = std::get_if<ThrowCopies>(&evaluation.exceptions->placements[b])) {
          q.witness.operation = caught->operation;
          q.Require(bundle.count == 0, ForInError::kCatchState);
        }
        q.Require(bundle.schedule.steps.empty(), ForInError::kCatchState);
      }
    }
    return q.initial - q.remaining;
  } catch (ForInFailure error) { return error;
  } catch (const std::bad_alloc&) { return ForInFailure{ForInError::kResourceLimit};
  } catch (const std::length_error&) { return ForInFailure{ForInError::kResourceLimit}; }
}
std::variant<std::uint64_t, ForInFailure> VerifyForInEmissionView(const ValueProgram& g,
    const ExceptionPlan& regions, const ExceptionControl& control, const ForInPlan& plan,
    const ForInEvents& events, const ForInEmissionView& view, SemanticBudget budget) {
  auto built = BuildForInEmissionView(g, regions, control, plan, events, budget);
  if (const auto* error = std::get_if<ForInFailure>(&built)) return *error;
  const auto& actual = std::get<ForInEmissionView>(built);
  auto remaining = budget.work - actual.work;
  for (const auto count : {view.continuations.size(), view.loops.size(), view.wrappers.size(),
      view.forwarded_regions.size(), view.forwarded_blocks.size(), view.forwarded_operations.size()}) {
    if (count > remaining) return ForInFailure{ForInError::kWorkLimit};
    remaining -= count;
  }
  if (actual.continuations != view.continuations || actual.loops != view.loops || actual.wrappers != view.wrappers ||
      actual.forwarded_regions != view.forwarded_regions || actual.forwarded_blocks != view.forwarded_blocks ||
      actual.forwarded_operations != view.forwarded_operations) return ForInFailure{ForInError::kCorrespondence};
  return budget.work - remaining;
}
}
