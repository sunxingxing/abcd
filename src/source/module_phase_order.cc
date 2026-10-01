#include "module_evaluation.h"

#include <new>
#include <stdexcept>
#include <type_traits>
#include "evaluation_flow.h"

namespace abcd::source {
namespace {
using namespace analysis;
using Error = EvaluationError;
void Require(bool value, Error error) { if (!value) throw error; }
class ModuleBudget {
 public:
  explicit ModuleBudget(SemanticBudget budget) : initial_(budget), remaining_(budget) {}
  void Step(std::uint64_t count = 1) {
    Require(count <= remaining_.work, Error::kWorkLimit); remaining_.work -= count;
  }
  template<class T> void Array(std::vector<T>& items, std::size_t count) {
    Require(count <= remaining_.bytes / sizeof(T), Error::kResourceLimit);
    Step(count); items.resize(count);
    Require(items.capacity() <= remaining_.bytes / sizeof(T), Error::kResourceLimit);
    remaining_.bytes -= items.capacity() * sizeof(T);
  }
  SemanticBudget Remaining() const { return remaining_; }
  std::uint64_t Work() const { return initial_.work - remaining_.work; }
 private:
  SemanticBudget initial_, remaining_;
};
EdgeId NormalEdge(const ValueProgram& graph, WorkBlockId id, ModuleBudget& budget) {
  const auto& block = graph.blocks[id.value];
  std::optional<EdgeId> found;
  for (std::uint32_t e = 0; e < block.edge_count; ++e) {
    budget.Step(); const EdgeId edge{block.first_edge.value + e};
    if (graph.edges[edge.value].kind == FlowEdgeKind::kException) continue;
    Require(!found && graph.edges[edge.value].kind == FlowEdgeKind::kFallthrough, Error::kOrder);
    found = edge;
  }
  Require(found.has_value(), Error::kOrder);
  return *found;
}
bool SameOperand(const ValueOperand& a, const ValueOperand& b) {
  if (a.index() != b.index()) return false;
  return std::visit([&](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, SpecialValue>) return value == std::get<T>(b);
    else return value.value == std::get<T>(b).value;
  }, a);
}
}  // namespace
std::variant<std::uint64_t, EvaluationError> VerifyModulePhaseOrder(
    const ValueProgram& graph, std::span<const ModuleEvaluation> modules, SemanticBudget allowance) {
  try {
    ModuleBudget budget(allowance);
    const auto flow = CheckEvaluationFlow(graph, EvaluationFlow::kPhased, budget.Remaining());
    if (const auto* error = std::get_if<Error>(&flow)) return *error;
    budget.Step(std::get<std::uint64_t>(flow));
    std::vector<std::uint32_t> incoming;
    std::vector<std::optional<std::size_t>> exits;
    budget.Array(incoming, graph.blocks.size()); budget.Array(exits, graph.ops.size());
    for (const auto& edge : graph.edges) { budget.Step(); ++incoming[edge.target.value]; }
    for (std::size_t i = 0; i < graph.exceptional_exits.size(); ++i) {
      budget.Step(); const auto op = graph.exceptional_exits[i].op;
      Require(op.value < exits.size() && !exits[op.value], Error::kInvalidGraph);
      exits[op.value] = i;
    }
    for (const auto& module : modules) {
      budget.Step();
      Require(module.pair.guard.has_value(), Error::kOrder);
      const auto read = module.pair.read, guard = *module.pair.guard;
      Require(read.value < graph.ops.size() && guard.value < graph.ops.size(), Error::kInvalidGraph);
      const auto before = graph.ops[read.value].block, after = graph.ops[guard.value].block;
      const auto commit = graph.edges[NormalEdge(graph, before, budget).value].target;
      Require(graph.phases[before.value].operation == read && graph.phases[after.value].operation == guard &&
          graph.phases[before.value].phase == ExceptionPhase::kBefore &&
          graph.phases[after.value].phase == ExceptionPhase::kBefore &&
          graph.phases[commit.value].phase == ExceptionPhase::kCommit &&
          graph.phases[commit.value].operation == read && graph.blocks[commit.value].op_count == 0 &&
          graph.blocks[commit.value].edge_count == 1 && incoming[after.value] == 1 &&
          graph.edges[NormalEdge(graph, commit, budget).value].target == after, Error::kOrder);
      Require(exits[read.value] && exits[guard.value] &&
          graph.exceptional_exits[*exits[read.value]].handler ==
          graph.exceptional_exits[*exits[guard.value]].handler, Error::kOrder);
    }
    return budget.Work();
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::variant<std::uint64_t, EvaluationError> VerifyModuleCatchCopies(
    const ValueProgram& graph, const EvaluationPlan& plan, SemanticBudget allowance) {
  try {
    if (plan.modules.empty() || plan.flow != EvaluationFlow::kPhased || !plan.phis) return std::uint64_t{0};
    Require(plan.exceptions.has_value(), Error::kLivePhi);
    ModuleBudget budget(allowance);
    // Called after independent phi/staging verification. Index once, then compare
    // equal slot sources; do not assume bundle copy order or scan per load.
    std::vector<std::optional<std::size_t>> normal, throws, staged;
    budget.Array(normal, graph.edges.size()); budget.Array(throws, graph.ops.size());
    budget.Array(staged, plan.exceptions->slots.size());
    for (std::size_t b = 0; b < plan.phis->bundles.size(); ++b) {
      budget.Step(); const auto& placement = plan.exceptions->placements[b];
      if (const auto* edge = std::get_if<NormalCopies>(&placement)) normal[edge->edge.value] = b;
      if (const auto* raised = std::get_if<ThrowCopies>(&placement)) throws[raised->operation.value] = b;
    }
    const auto count = [&](std::optional<std::size_t> bundle) {
      return bundle ? plan.phis->bundles[*bundle].count : 0U;
    };
    for (const auto& module : plan.modules) {
      budget.Step(); Require(module.pair.guard.has_value(), Error::kOrder);
      const auto read = module.pair.read, guard = *module.pair.guard;
      const auto edge = NormalEdge(graph, graph.ops[read.value].block, budget);
      const auto commit = graph.edges[edge.value].target;
      Require(!normal[edge.value] && !normal[NormalEdge(graph, commit, budget).value], Error::kOrder);
      const auto load_bundle = throws[read.value], guard_bundle = throws[guard.value];
      Require(count(load_bundle) == count(guard_bundle), Error::kOrder);
      if (count(load_bundle) == 0) continue;
      const auto& load = plan.phis->bundles[*load_bundle];
      const auto& check = plan.phis->bundles[*guard_bundle];
      for (std::size_t i = load.first; i < std::size_t(load.first) + load.count; ++i) {
        budget.Step(); staged[plan.exceptions->staging[i]->value] = i;
      }
      for (std::size_t i = check.first; i < std::size_t(check.first) + check.count; ++i) {
        budget.Step(); const auto prior = staged[plan.exceptions->staging[i]->value];
        Require(prior && *prior >= load.first && *prior < std::size_t(load.first) + load.count &&
            SameOperand(plan.phis->copies[*prior].source, plan.phis->copies[i].source), Error::kOrder);
      }
    }
    // Guard staging may still execute on normal flow, but a TDZ throw at the
    // fused read sees exactly the load's already staged values in every slot.
    return budget.Work();
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
