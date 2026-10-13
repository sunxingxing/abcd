#include "../../../src/source/control_skeleton.h"
#include "../../../src/source/control_failure_json.h"
#include "../../../src/source/family_route_failure.h"

#include <array>
#include <cassert>
#include <iostream>

using namespace abcd;
using namespace abcd::source;
namespace {
using Targets = std::array<std::optional<std::uint32_t>, 2>;
analysis::ValueProgram Graph(const std::vector<Targets>& targets) {
  analysis::ValueProgram graph;
  for (std::uint32_t i = 0; i < targets.size(); ++i) {
    analysis::WorkBlock block{{static_cast<std::uint32_t>(graph.ops.size())}, 0,
        {static_cast<std::uint32_t>(graph.edges.size())}, 0};
    for (std::uint8_t e = 0; e < 2; ++e) if (targets[i][e])
      graph.edges.push_back({{i}, {*targets[i][e]}, e ? analysis::FlowEdgeKind::kFallthrough :
          analysis::FlowEdgeKind::kTaken, block.edge_count++});
    if (!block.edge_count) {
      analysis::ValueOp op{}; op.block = {i}; op.operation = analysis::ValueReturn{};
      graph.ops.push_back(op); block.op_count = 1;
    }
    graph.blocks.push_back(block);
  }
  return graph;
}
struct Example { analysis::ValueProgram graph; std::uint32_t shared, fallback, stop; };
Example Decision(std::uint32_t count, bool reverse = false) {
  // One maximal short-circuit chain, an arbitrary private region of diamonds,
  // and a continuing region with joins. No body is copied per condition.
  const auto fallback = count + 1, shared = fallback + 4 * count, stop = shared + 4 * count;
  std::vector<Targets> edges(stop + 1);
  edges[0] = {stop, 1};
  for (std::uint32_t i = 1; i <= count; ++i) {
    edges[i] = {shared, i == count ? fallback : i + 1};
    if (reverse) std::swap(edges[i][0], edges[i][1]);
  }
  for (const auto start : {fallback, shared}) {
    for (std::uint32_t i = 0; i < count; ++i) {
      const auto b = start + 4 * i;
      edges[b] = {b + 1, b + 2}; edges[b + 1] = {b + 3, {}}; edges[b + 2] = {b + 3, {}};
      if (i + 1 < count || start == shared) edges[b + 3] = {b + 4, {}};
    }
  }
  return {Graph(edges), shared, fallback, stop};
}
analysis::ValueProgram Split(const analysis::ValueProgram& graph) {
  std::vector<Targets> edges(graph.blocks.size());
  for (const auto& edge : graph.edges) {
    const auto b = static_cast<std::uint32_t>(edges.size());
    edges.push_back({edge.target.value, {}}); edges[edge.source.value][edge.successor] = b;
  }
  auto result = Graph(edges);
  for (std::uint32_t i = graph.blocks.size(); i < result.blocks.size(); ++i) {
    auto& b = result.blocks[i]; b.first_op = {static_cast<std::uint32_t>(result.ops.size())}; b.op_count = 1;
    analysis::ValueOp op{}; op.block = {i}; op.operation = analysis::ValueCopy{}; result.ops.push_back(op);
  }
  return result;
}
void Rejected(const analysis::ValueProgram& graph, const ControlPlan& plan) {
  assert(std::holds_alternative<ControlError>(VerifyControlPlan(graph, plan)));
}
void Positive() {
  std::uint64_t previous = 0;
  for (const auto count : {2U, 200U, 201U, 400U, 800U}) {
    for (const bool reverse : {false, true}) {
      const auto test = Decision(count, reverse);
      const auto built = BuildControlPlan(test.graph);
      assert(std::holds_alternative<ControlPlan>(built));
      const auto& plan = std::get<ControlPlan>(built);
      assert(plan.nodes[0].next == ControlId{test.stop} && !plan.nodes[1].next);
      assert(plan.nodes[1].decision && plan.nodes[1].decision->join == ControlId{test.stop});
      assert(std::holds_alternative<std::uint64_t>(VerifyControlPlan(test.graph, plan)));
      // The exact previous private-return join must not pass the independent verifier.
      auto bad = plan; bad.nodes[1].next = {test.fallback};
      for (auto& node : bad.nodes) if (node.decision) node.decision->join = {test.fallback};
      Rejected(test.graph, bad);
      bad = plan; bad.nodes[1].decision->shared_on_taken = !bad.nodes[1].decision->shared_on_taken;
      Rejected(test.graph, bad);
      bad = plan; bad.nodes[count].arms.reset(); Rejected(test.graph, bad);
      bad = plan; bad.nodes[test.shared].next = {test.stop}; Rejected(test.graph, bad);
      bad = plan; bad.edges[0].owner = {1}; Rejected(test.graph, bad);
      const auto split = Split(test.graph);
      const auto split_built = BuildControlPlan(split);
      assert(std::holds_alternative<ControlPlan>(split_built));
      assert(std::holds_alternative<std::uint64_t>(VerifyControlPlan(split, std::get<ControlPlan>(split_built))));
      // The same graph through the shared projected view (as used by phased/loop control).
      std::vector<ControlVertex> nodes; std::vector<ControlLink> arcs;
      const ControlGraphView normal(test.graph);
      for (std::uint32_t n = 0; n < normal.NodeCount(); ++n) nodes.push_back(normal.Node({n}));
      for (std::uint32_t e = 0; e < normal.ArcCount(); ++e) arcs.push_back(normal.Arc({e}));
      const ControlGraphView view(ControlProjection{test.graph.revision, test.graph.cfg_revision, nodes, arcs});
      ControlQuota quota({}); const ControlId root{0};
      const auto projected = BuildControlSkeleton(view, {&root, 1}, quota);
      VerifyControlSkeleton(view, {&root, 1}, projected, quota);
      assert(projected.nodes[1].decision->join == ControlId{test.stop});
      // A forged abrupt label on a normal exit port must not certify the arm.
      nodes[test.shared - 1].termination = ControlTermination::kNormal;
      bool refused_port = false;
      try { VerifyControlSkeleton(view, {&root, 1}, projected, quota); }
      catch (ControlError) { refused_port = true; }
      assert(refused_port);
      auto budget = analysis::SemanticBudget{}; budget.work = plan.work;
      assert(std::holds_alternative<ControlPlan>(BuildControlPlan(test.graph, budget)));
      --budget.work; assert(ControlResultError(BuildControlPlan(test.graph, budget)) == ControlError::kWorkLimit);
      if (!reverse) {
        if (count >= 400) assert(plan.work < previous * 3);
        previous = plan.work;
        std::cout << count << " nodes=" << test.graph.blocks.size() << " work=" << plan.work << '\n';
      }
    }
  }
}
void Negative() {
  const auto test = Decision(2);
  const auto plan = std::get<ControlPlan>(BuildControlPlan(test.graph));
  auto changed = test.graph;
  changed.edges[0].target = {test.fallback + 1}; // Outside entry into private effects.
  Rejected(changed, plan);
  changed = test.graph;
  changed.ops[changed.blocks[test.fallback + 7].first_op.value].operation = analysis::ValueNop{};
  Rejected(changed, plan); // Normal/unknown completion is not a return.
  changed = test.graph;
  changed.edges[changed.blocks[test.shared + 3].first_edge.value].target = {test.fallback};
  Rejected(changed, plan); // Shared tail cannot acquire the private fallback.
  changed = test.graph;
  changed.edges.back().target = {1}; Rejected(changed, plan); // Backedge is not termination.
  const auto returns = Graph({{{6,1}}, {{4,2}}, {{4,3}}, {}, {{5,{}}}, {{6,{}}}, {}});
  const auto return_plan = std::get<ControlPlan>(BuildControlPlan(returns));
  const auto continues = Graph({{{6,1}}, {{4,2}}, {{4,3}}, {{6,{}}}, {{5,{}}}, {{6,{}}}, {}});
  Rejected(continues, return_plan); // R's return was replaced by a normal continuation.
  const auto continuing_plan = std::get<ControlPlan>(BuildControlPlan(continues));
  assert(std::holds_alternative<std::uint64_t>(VerifyControlPlan(continues, continuing_plan)));

  // Some continuing paths return before T. Lexical follow reaches T, but real
  // postdominance does not. This increment refuses rather than trusting follow.
  auto bypass = Graph({{{6,1}}, {{4,2}}, {{4,3}}, {}, {{7,5}}, {}, {}, {{6,{}}}});
  for (std::uint32_t i = 0; i < bypass.ops.size(); ++i) {
    bypass.ops[i].origin = {i}; bypass.origins.push_back({{i}, {i + 20}, abc::CodeOffset{i + 120}});
  }
  const auto refused = BuildControlPlan(bypass);
  assert(std::holds_alternative<ControlFailure>(refused));
  const auto& failure = std::get<ControlFailure>(refused);
  assert(std::get<DecisionContinuationError>(failure.cause) == DecisionContinuationError::kPostdominator);
  assert(failure.control.decision && failure.control.decision->entry.node == 1 &&
      failure.control.decision->predicate.node == 2 && failure.control.decision->shared.node == 4 &&
      failure.control.decision->fallback.node == 3 && failure.control.decision->old_join.node == 3 &&
      failure.control.decision->enclosing_stop.node == 6 && failure.control.decision->witness.node == 6);
  assert(failure.control.node.pc == abc::CodeOffset{122} && failure.control.node.working_pc == abc::CodeOffset{22});
  FailureCauseChain causes; CollectFailureCauses(failure, causes);
  assert(causes.View().size() == 2 && causes.View().back().domain == FailureDomain<DecisionContinuationError>());
  FamilyTreeFailure family{{0}, failure}; family.method = abc::EntityOffset{700};
  const auto routed = FamilyRouteFailure(FamilyPrepareFailure{family});
  assert(routed.method == abc::EntityOffset{700} && routed.pc == abc::CodeOffset{122} &&
      routed.working_pc == abc::CodeOffset{22} && routed.control == failure.control &&
      routed.causes.View().back().domain == FailureDomain<DecisionContinuationError>());
  for (const auto kind : {ControlError::kResourceLimit, ControlError::kWorkLimit}) {
    auto resource = failure; resource.kind = kind; family.cause = resource;
    const auto route = FamilyRouteFailure(FamilyPrepareFailure{family});
    assert(route.failure_class == (kind == ControlError::kResourceLimit ?
        RouteFailureClass::kMemoryLimit : RouteFailureClass::kWorkLimit));
  }
  const auto json = ControlJson(failure.control);
  assert(ParseControlJson(json) == failure.control);
  for (const auto* field : {"entry", "predicate", "shared", "fallback", "old_join", "enclosing_stop", "witness"}) {
    auto invalid = json; invalid["decision"].erase(field); assert(!ParseControlJson(invalid));
    invalid = json; invalid["decision"][field]["origin"] = 99; assert(!ParseControlJson(invalid));
  }
  auto mapped = failure.control;
  MapControlLocations(mapped, [](ControlLocation& loc) {
    loc.origin = ControlOriginKind::kPhase; loc.pc = abc::CodeOffset{loc.node + 100};
    loc.working_pc = abc::CodeOffset{loc.node + 10};
  });
  assert(mapped.decision->predicate.pc->value == 102 && mapped.decision->enclosing_stop.working_pc->value == 16);
  assert(ParseControlJson(ControlJson(mapped)) == mapped);
}
void Kept() {
  // Exact old layout: shared body joins the common normal suffix, so nothing is
  // replaced, including the final condition's absent fallback and next fields.
  const auto graph = Graph({{{2,1}}, {{2,3}}, {{3,{}}}, {}});
  const auto plan = std::get<ControlPlan>(BuildControlPlan(graph));
  assert(plan.nodes.size() == 4 && plan.edges.size() == 5);
  for (std::uint32_t i = 0; i < 4; ++i) {
    const auto& n = plan.nodes[i]; assert(n.block.value == i && std::holds_alternative<std::monostate>(n.synthetic));
    assert(n.next == (i == 0 ? std::optional<ControlId>{{3}} : std::nullopt));
    assert(n.arms.has_value() == (i < 2));
    if (n.arms) { assert(n.arms->taken == ControlId{2}); assert(!n.arms->fallthrough); }
    assert(n.decision.has_value() == (i == 0));
  }
  const auto& d = *plan.nodes[0].decision;
  assert(d.evaluate == ControlId{1} && !d.evaluate_on_taken && d.shared_on_taken &&
      d.join == ControlId{3} && !d.predicate && d.prefix == DecisionPrefix::kLinear);
  for (std::uint32_t e = 0; e < 5; ++e) {
    assert(plan.edges[e].edge.value == e);
    assert(plan.edges[e].owner.value == (e < 2 ? 0 : e < 4 ? 1 : 2));
    assert(plan.edges[e].placement == (e == 4 ? EdgePlacement::kJoin : e % 2 ? EdgePlacement::kFallthrough : EdgePlacement::kTaken));
  }
}
void Bytes() {
  for (const auto count : {200U,201U,400U,800U}) {
    const auto graph = Decision(count).graph; auto quota = analysis::SemanticBudget{};
    std::size_t lo = 0, hi = quota.bytes;
    while (lo < hi) {
      quota.bytes = lo + (hi - lo) / 2;
      const auto result = BuildControlPlan(graph, quota);
      if (std::holds_alternative<ControlPlan>(result)) hi = quota.bytes;
      else { assert(ControlResultError(result) == ControlError::kResourceLimit); lo = quota.bytes + 1; }
    }
    quota.bytes = lo; assert(std::holds_alternative<ControlPlan>(BuildControlPlan(graph, quota)));
    --quota.bytes; assert(ControlResultError(BuildControlPlan(graph, quota)) == ControlError::kResourceLimit);
  }
}
}
int main() { Kept(); Negative(); Positive(); Bytes(); }
