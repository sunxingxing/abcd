#include "analysis/guarded_fixture.h"
#include "../../../src/analysis/guarded_reads.h"
#include "../../../src/analysis/phased_value_import.h"
#include "abcd/analysis/exception_captures.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
ValueProgram Graph(std::size_t count, bool copies = false, bool split = false, bool escape = false) {
  return test::WithGuardedGraph(count, copies, split, escape,
      [](const auto& graph, const auto&, const auto&) { return graph; });
}
std::pair<std::uint64_t, std::size_t> Run(std::size_t count, bool copies, bool split) {
  auto graph = Graph(count, copies, split);
  const auto built = ProveGuardedReads(graph);
  test::Check(std::holds_alternative<GuardedReads>(built));
  const auto& plan = std::get<GuardedReads>(built);
  test::Check(plan.reads.size() == count);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyGuardedReads(graph, plan)));
  auto changed = plan; changed.reads.front().guard = changed.reads.front().read;
  test::Check(std::get<GuardedReadError>(VerifyGuardedReads(graph, changed)) == GuardedReadError::kCorrespondence);
  changed = plan; changed.reads.pop_back();
  test::Check(std::get<GuardedReadError>(VerifyGuardedReads(graph, changed)) == GuardedReadError::kCorrespondence);
  SemanticBudget budget; budget.work = plan.work;
  test::Check(std::holds_alternative<GuardedReads>(ProveGuardedReads(graph, budget)));
  --budget.work;
  test::Check(std::get<GuardedReadError>(ProveGuardedReads(graph, budget)) == GuardedReadError::kWorkLimit);
  budget = {}; budget.bytes = 0;
  test::Check(std::get<GuardedReadError>(ProveGuardedReads(graph, budget)) == GuardedReadError::kResourceLimit);
  // Exact minimum admission, using a slow test-only search. Production does
  // not probe budgets, rebuild the graph, or walk one CFG per capture.
  std::size_t low = 0, high = plan.allocated_bytes;
  while (low + 1 < high) {
    budget.bytes = low + (high - low) / 2;
    if (std::holds_alternative<GuardedReads>(ProveGuardedReads(graph, budget))) high = budget.bytes;
    else low = budget.bytes;
  }
  budget.bytes = high; test::Check(std::holds_alternative<GuardedReads>(ProveGuardedReads(graph, budget)));
  budget.bytes = high - 1;
  test::Check(std::get<GuardedReadError>(ProveGuardedReads(graph, budget)) == GuardedReadError::kResourceLimit);
  graph.ops[plan.reads.front().guard.value].life = ValueLife::kRemoved;
  test::Check(std::get<GuardedReadError>(VerifyGuardedReads(graph, plan)) == GuardedReadError::kCorrespondence);
  return {plan.work, plan.allocated_bytes};
}
}
int main() {
  std::pair<std::uint64_t, std::size_t> previous{};
  for (const auto n : {1U, 200U, 201U, 400U, 800U}) {
    const auto cost = Run(n, false, false);
    if (n > 200) test::Check(cost.first <= 3 * previous.first && cost.second <= 3 * previous.second);
    previous = cost;
  }
  Run(1, true, false); Run(1, true, true);
  test::WithGuardedGraph(1, false, false, false, [](const auto& graph, const auto&, const auto&) {
    const auto proof = std::get<GuardedReads>(ProveGuardedReads(graph));
    test::Check(proof.reads.size() == 1 && proof.reads.front().read > proof.reads.front().guard);
  }, true);
  const auto graph = Graph(1, true, false, true);
  test::Check(std::get<GuardedReads>(ProveGuardedReads(graph)).reads.empty());
}
