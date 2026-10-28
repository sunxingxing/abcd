#include <deque>
#include <set>

#include "recovery_flow.h"
#include "recovery_scratch.h"
namespace abcd::source {
void VerifySinglePassRecovery(const OriginalFlow& raw,
                              const OriginalIterations& native,
                              const RecoveryFlow& flow,
                              TreeCheckBudget& budget) {
  const auto need = [](bool b) {
    RequireTree(b, TreeStructureError::kOwnership);
  };
  if (!native.view.single_pass) {
    need(!native.single_pass_facts);
    return;
  }
  need(bool(native.single_pass_facts));
  const auto& p = *native.view.single_pass;
  const auto& expected = *native.single_pass_facts;
  budget.Step(p.scopes.size() + p.parents.size() + p.coverage.size() +
              p.cuts.size() + p.dead_entries.size() + p.step_exits.size() +
              p.protection.size() + p.parents_protection.size() +
              p.dispatch.size());
  need(p.revision == raw.value_revision && p.cfg_revision == raw.cfg_revision &&
       p.scopes == expected.scopes && p.parents == expected.parents &&
       p.coverage == expected.coverage && p.cuts == expected.cuts &&
       p.dead_entries == expected.dead_entries &&
       p.step_exits == expected.step_exits &&
       p.protection == expected.protection &&
       p.parents_protection == expected.parents_protection &&
       p.dispatch == expected.dispatch);
  constexpr std::size_t kScopeVisitsPerNode = 8;
  RecoveryScratch scratch(budget);
  std::pmr::vector<std::optional<analysis::WorkBlockId>> blocks(
      raw.phases.size(), {}, &scratch);
  for (std::uint32_t b = 0; b < raw.phases.size(); ++b) {
    budget.Step();
    const auto n = raw.phases[b].input;
    need(n.value < blocks.size() && !blocks[n.value]);
    blocks[n.value] = {b};
  }
  for (const auto& dead : p.dead_entries) {
    budget.Step();
    need(dead.source.value < blocks.size() && blocks[dead.source.value] &&
         raw.phases[blocks[dead.source.value]->value].reachability ==
             analysis::PhaseReachability::kUnreachable);
  }
  for (const auto& s : p.scopes)
    if (s) {
      budget.Step();
      need(s->protocol.value < native.loops.size());
      const auto& l = native.loops[s->protocol.value];
      need(s->acquisition == l.acquisition && s->advance == l.advance &&
           s->decision == l.decision && s->value == l.value &&
           s->exhausted == l.exhausted);
    }
  // Actual native flow, not a builder flag: following the body's fallthrough
  // would reach the implicit next event. It must be unreachable. Entering the
  // current iterator's close leaves the activation; existing close/port checks
  // separately prove every priority outcome and enclosing completion route.
  std::pmr::vector<std::optional<std::uint32_t>> closes(flow.events.size(), {},
                                                        &scratch);
  for (std::uint32_t n = 0; n < flow.iteration_closes.size(); ++n) {
    budget.Step();
    const auto at = flow.iteration_closes[n].entry;
    need(at.value < closes.size() && !closes[at.value]);
    closes[at.value] = n;
  }
  using Query = std::pair<std::uint32_t, std::uint32_t>;
  std::pmr::set<Query> seen(&scratch);
  std::pmr::deque<Query> pending(&scratch);
  const auto dimension =
      flow.events.size() + flow.edges.size() + p.coverage.size() + 1;
  need(dimension <= SIZE_MAX / kScopeVisitsPerNode);
  const auto limit = dimension * kScopeVisitsPerNode;
  std::size_t visits = 0;
  const auto step = [&]() {
    budget.Step();
    RequireTree(visits++ < limit, TreeStructureError::kResourceLimit);
  };
  const auto add = [&](Query query) {
    step();
    if (seen.insert(query).second) pending.push_back(query);
  };
  for (const auto& s : p.scopes)
    if (s) {
      budget.Step();
      need(s->protocol.value < native.statements.size());
      const auto statement = native.statements[s->protocol.value];
      need(statement.value < flow.iterations.size() &&
           flow.iterations[statement.value]);
      add({s->protocol.value, flow.iterations[statement.value]->body.value});
    }
  while (!pending.empty()) {
    step();
    const auto [protocol, point] = pending.front();
    pending.pop_front();
    const auto statement = native.statements[protocol];
    const auto& scope = *flow.iterations[statement.value];
    need(point < flow.events.size());
    need(point !=
             scope.events[static_cast<std::size_t>(ForOfEventKind::kNextCall)]
                 .before.value &&
         point !=
             scope.events[static_cast<std::size_t>(ForOfEventKind::kAcquire)]
                 .before.value);
    if (point == scope.exit.value || point == scope.thrown.value ||
        point == flow.terminal.value)
      continue;
    if (closes[point]) {
      const auto& close = flow.iteration_closes[*closes[point]];
      if (close.loop == statement) {
        need(
            close.pending != analysis::PendingCompletionKind::kNormal &&
            close.target !=
                scope
                    .events[static_cast<std::size_t>(ForOfEventKind::kNextCall)]
                    .before);
        continue;
      }
    }
    for (auto e = flow.events[point].outgoing; e; e = flow.edges[*e].outgoing) {
      step();
      add({protocol, flow.edges[*e].to.value});
    }
  }
}
}  // namespace abcd::source
