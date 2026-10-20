#include "switch_region.h"

#include <algorithm>
#include <limits>

#include "../analysis/value_phi.h"
#include "abcd/analysis/dominators.h"
#include "cell_evaluation.h"
namespace abcd::source {
namespace {
using namespace analysis;
using Error = ControlError;
void Require(bool b) {
  if (!b) throw Error::kCrossEntry;
}
struct Test {
  OpId branch, comparison;
  std::array<ValueOperand, 2> operands;
  EdgeId match, miss;
};
struct View {
  SwitchRegions proof;
  std::vector<ControlVertex> nodes;
  std::vector<ControlLink> arcs;
  ControlLoopLayout loops;
};
DominatorTree Dominators(const ValueProgram& g, bool reverse, ControlQuota& q) {
  const auto n = static_cast<std::uint32_t>(g.blocks.size());
  std::vector<std::uint32_t> first, cursor;
  std::vector<BlockId> links;
  q.Size(first, n + 2);
  q.Size(cursor, n + 1);
  std::size_t sinks = 0;
  for (std::uint32_t b = 0; b < n; ++b) {
    q.Work();
    if (!g.blocks[b].edge_count) ++sinks;
  }
  q.Size(links, g.edges.size() + (reverse ? sinks : 0));
  for (const auto& e : g.edges) {
    q.Work();
    ++first[(reverse ? e.target.value : e.source.value) + 1];
  }
  if (reverse) first[n + 1] = static_cast<std::uint32_t>(sinks);
  for (std::uint32_t b = 0; b <= n; ++b) {
    q.Work();
    first[b + 1] += first[b];
    cursor[b] = first[b];
  }
  for (const auto& e : g.edges) {
    q.Work();
    const auto from = reverse ? e.target : e.source,
               to = reverse ? e.source : e.target;
    links[cursor[from.value]++] = {to.value};
  }
  if (reverse)
    for (std::uint32_t b = 0; b < n; ++b) {
      q.Work();
      if (!g.blocks[b].edge_count) links[cursor[n]++] = {b};
    }
  auto r = BuildDominators({first, links}, {reverse ? n : 0},
                           {q.Remaining().bytes, q.RemainingWork()});
  if (const auto* e = std::get_if<DominatorError>(&r)) {
    if (*e == DominatorError::kWorkLimit) throw Error::kWorkLimit;
    if (*e == DominatorError::kResourceLimit) throw Error::kResourceLimit;
    throw Error::kInvalidGraph;
  }
  auto tree = std::get<DominatorTree>(std::move(r));
  q.Work(tree.work.steps);
  q.Admit(tree.work.allocated_bytes, 1);
  return tree;
}
DominatorTree EdgeDominators(const ValueProgram& g, ControlQuota& q) {
  Require(g.blocks.size() <=
          std::numeric_limits<std::uint32_t>::max() - g.edges.size());
  const auto n = static_cast<std::uint32_t>(g.blocks.size()),
             size = static_cast<std::uint32_t>(n + g.edges.size());
  std::vector<std::uint32_t> first, cursor;
  std::vector<BlockId> links;
  q.Size(first, static_cast<std::size_t>(size) + 1);
  q.Size(cursor, size);
  q.Size(links, 2 * g.edges.size());
  for (std::uint32_t e = 0; e < g.edges.size(); ++e) {
    q.Work();
    ++first[g.edges[e].source.value + 1];
    ++first[n + e + 1];
  }
  for (std::uint32_t b = 0; b < size; ++b) {
    q.Work();
    first[b + 1] += first[b];
    cursor[b] = first[b];
  }
  for (std::uint32_t e = 0; e < g.edges.size(); ++e) {
    q.Work();
    links[cursor[g.edges[e].source.value]++] = {n + e};
    links[cursor[n + e]++] = {g.edges[e].target.value};
  }
  auto result = BuildDominators({first, links}, {0},
                                {q.Remaining().bytes, q.RemainingWork()});
  if (const auto* e = std::get_if<DominatorError>(&result)) {
    if (*e == DominatorError::kWorkLimit) throw Error::kWorkLimit;
    if (*e == DominatorError::kResourceLimit) throw Error::kResourceLimit;
    throw Error::kInvalidGraph;
  }
  auto tree = std::get<DominatorTree>(std::move(result));
  q.Work(tree.work.steps);
  q.Admit(tree.work.allocated_bytes, 1);
  return tree;
}
bool Inert(const ValueProgram& g, std::uint32_t b, ControlQuota& q) {
  const auto& block = g.blocks[b];
  if (block.edge_count != 1) return false;
  for (std::uint32_t i = 0; i < block.op_count; ++i) {
    q.Work();
    const auto& o = g.ops[block.first_op.value + i];
    if (o.life == ValueLife::kLive &&
        (o.result || o.use_count || IsEnvironmentState(o.operation) ||
         o.normal != LanguageEffect::kNone ||
         o.before_throw != LanguageEffect::kNone ||
         o.exceptional != ExceptionalCompletion::kImpossible))
      return false;
  }
  return true;
}
View Derive(const ValueProgram& g, const ControlFailure& seed,
            ControlQuota& q) {
  Require(g.phases.empty() && !g.blocks.empty());
  const auto n = static_cast<std::uint32_t>(g.blocks.size());
  auto ids = AnalyzeValueIdentities(g, q.Remaining());
  if (const auto* e = std::get_if<ValueSimplifyError>(&ids)) {
    if (*e == ValueSimplifyError::kResourceLimit) throw Error::kResourceLimit;
    if (*e == ValueSimplifyError::kWorkLimit) throw Error::kWorkLimit;
    throw Error::kInvalidGraph;
  }
  auto identity = std::get<ValueIdentities>(std::move(ids));
  q.Work(identity.work);
  q.Admit(identity.allocated_bytes, 1);
  const auto canonical = [&](ValueOperand v) {
    if (auto* p = std::get_if<ValueId>(&v)) *p = identity.canonical[p->value];
    return v;
  };
  auto dom = Dominators(g, false, q), post = Dominators(g, true, q);
  auto edge_dom = EdgeDominators(g, q);
  auto loop_result = BuildLoopForest(g, q.Remaining());
  if (const auto* e = std::get_if<LoopError>(&loop_result)) {
    if (*e == LoopError::kWorkLimit) throw Error::kWorkLimit;
    if (*e == LoopError::kResourceLimit) throw Error::kResourceLimit;
    throw Error::kCrossEntry;
  }
  auto forest = std::get<LoopForest>(std::move(loop_result));
  q.Work(forest.work);
  q.Admit(forest.allocated_bytes, 1);
  // Empty dispatch corridors may carry neither SSA transfers nor lexical
  // activation boundaries. This census is shared by all candidate switches.
  std::vector<std::uint8_t> inert;
  q.Size(inert, n);
  for (std::uint32_t b = 0; b < n; ++b) {
    q.Work();
    inert[b] = Inert(g, b, q);
  }
  for (const auto& value : g.values) {
    q.Work();
    if (value.life != ValueLife::kLive) continue;
    if (const auto* p = std::get_if<PhiDefinition>(&value.origin))
      inert[p->block.value] = 0;
    if (const auto* c = std::get_if<CatchDefinition>(&value.origin))
      inert[c->block.value] = 0;
  }
  for (const auto& use : g.uses) {
    q.Work();
    if (use.life == ValueLife::kLive && use.incoming)
      if (const auto* e = std::get_if<EdgeId>(&*use.incoming))
        inert[g.edges[e->value].source.value] = 0;
  }
  for (const auto& loop : forest.loops) {
    q.Work();
    inert[loop.header.value] = 0;
  }
  std::vector<std::optional<Test>> tests;
  std::vector<std::optional<std::uint32_t>> ends, parents;
  std::vector<std::uint8_t> state;
  std::vector<std::uint32_t> stack, roots, case_root, case_stamp, queue;
  std::vector<std::optional<std::uint32_t>> test_clause;
  q.Size(tests, n);
  q.Size(ends, n);
  q.Size(parents, n);
  q.Size(state, n);
  q.Size(stack, n);
  q.Size(roots, n);
  q.Size(case_root, n);
  q.Size(case_stamp, n);
  q.Size(queue, n);
  q.Size(test_clause, n);
  for (std::uint32_t b = 0; b < n; ++b) {
    q.Work();
    const auto& block = g.blocks[b];
    if (block.edge_count != 2 || !block.op_count) continue;
    const OpId branch{block.first_op.value + block.op_count - 1};
    const auto& o = g.ops[branch.value];
    const auto* br = std::get_if<ValueBranch>(&o.operation);
    if (!br || o.use_count != 1) continue;
    if (br->predicate != UnaryOperator::kFalseOrZero &&
        br->predicate != UnaryOperator::kTrueOrNonzeroNumber)
      continue;
    const auto operand = canonical(g.uses[o.first_use.value].operand);
    const auto* v = std::get_if<ValueId>(&operand);
    const auto* def =
        v ? std::get_if<OpResult>(&g.values[v->value].origin) : nullptr;
    if (!def) continue;
    const auto& cmp = g.ops[def->op.value];
    const auto* binary = std::get_if<ValueBinary>(&cmp.operation);
    if (!binary || cmp.use_count != 2 ||
        (binary->op != BinaryOperator::kStrictEqual &&
         binary->op != BinaryOperator::kStrictNotEqual))
      continue;
    const bool match_taken =
        (binary->op == BinaryOperator::kStrictEqual) ==
        (br->predicate == UnaryOperator::kTrueOrNonzeroNumber);
    tests[b] = Test{branch,
                    def->op,
                    {canonical(g.uses[cmp.first_use.value].operand),
                     canonical(g.uses[cmp.first_use.value + 1].operand)},
                    {block.first_edge.value + (match_taken ? 0U : 1U)},
                    {block.first_edge.value + (match_taken ? 1U : 0U)}};
  }
  // Memoized linear evaluation corridors. Branching case expressions remain
  // ordinary until their expression-region proof can supply a complete view.
  for (std::uint32_t b = 0; b < n; ++b) {
    q.Work();
    if (state[b]) continue;
    std::size_t size = 0;
    auto at = b;
    while (!state[at]) {
      q.Work();
      state[at] = 1;
      stack[size++] = at;
      if (tests[at] || g.blocks[at].edge_count != 1) break;
      const auto next = g.edges[g.blocks[at].first_edge.value].target.value;
      if (state[next] == 1) break;
      at = next;
    }
    std::optional<std::uint32_t> end = tests[at]        ? std::optional{at}
                                       : state[at] == 2 ? ends[at]
                                                        : std::nullopt;
    while (size) {
      q.Work();
      const auto x = stack[--size];
      ends[x] = end;
      state[x] = 2;
    }
  }
  const auto same = [&](const Test& a,
                        const Test& b) -> std::optional<ValueId> {
    for (const auto& x : a.operands)
      if (const auto* v = std::get_if<ValueId>(&x))
        for (const auto& y : b.operands)
          if (const auto* w = std::get_if<ValueId>(&y); w && *v == *w)
            return *v;
    return {};
  };
  for (std::uint32_t b = 0; b < n; ++b)
    if (tests[b]) {
      q.Work();
      const auto next = ends[g.edges[tests[b]->miss.value].target.value];
      if (next && same(*tests[b], *tests[*next]) &&
          edge_dom.Dominates({n + tests[b]->miss.value}, {*next})) {
        if (parents[*next])
          parents[*next] = n;
        else
          parents[*next] = b;
      }
    }
  std::size_t root_count = 0;
  for (std::uint32_t b = 0; b < n; ++b)
    if (tests[b] && !parents[b]) {
      q.Work();
      const auto next = ends[g.edges[tests[b]->miss.value].target.value];
      if (next && same(*tests[b], *tests[*next]) &&
          edge_dom.Dominates({n + tests[b]->miss.value}, {*next}))
        roots[root_count++] = b;
    }
  std::sort(roots.begin(), roots.begin() + root_count, [&](auto a, auto b) {
    q.Work();
    return dom.nodes[a].begin < dom.nodes[b].begin;
  });
  View v;
  v.proof.seed = seed;
  q.Size(v.proof.blocks, n);
  q.Size(v.proof.regions, n);
  q.Size(v.proof.clauses, n + 1);
  q.Size(v.proof.ports, g.edges.size());
  std::size_t region_count = 0, clause_count = 0, port_count = 0;
  // A discovery vertex is visited by at most one disjoint region. Nested
  // unrepresented switches keep their ordinary typed ownership refusal.
  for (std::size_t ri = 0; ri < root_count; ++ri) {
    q.Work();
    const auto root = roots[ri];
    if (v.proof.blocks[root]) continue;
    const auto next = ends[g.edges[tests[root]->miss.value].target.value];
    Require(next.has_value());
    const auto disc = same(*tests[root], *tests[*next]);
    Require(disc.has_value());
    auto follow = post.nodes[root].parent;
    std::optional<WorkBlockId> exit =
        follow && follow->value < n && dom.Dominates({root}, *follow)
            ? std::optional<WorkBlockId>{{follow->value}}
            : std::nullopt;
    if (!exit && seed.control.node.origin == ControlOriginKind::kBlock &&
        seed.control.node.source < n &&
        dom.Dominates({root}, {seed.control.node.source})) {
      const auto stop = seed.control.current.stop;
      if (stop && stop->origin == ControlOriginKind::kBlock && stop->source < n)
        exit = WorkBlockId{stop->source};
    }
    // When an abrupt case has no normal postdominator, the ownership census
    // supplies the first shared continuation as a candidate follow. A match
    // entry can never be this follow: it is an actual later case body.
    if (seed.control.node.origin == ControlOriginKind::kBlock &&
        seed.control.node.source < n &&
        dom.Dominates({root}, {seed.control.node.source}) &&
        (!exit || !dom.Dominates({exit->value}, {seed.control.node.source}))) {
      bool case_entry = false;
      auto test = root;
      while (true) {
        q.Work();
        case_entry |= g.edges[tests[test]->match.value].target.value ==
                      seed.control.node.source;
        const auto next_test =
            ends[g.edges[tests[test]->miss.value].target.value];
        if (!next_test || !same(*tests[test], *tests[*next_test]) ||
            !edge_dom.Dominates({n + tests[test]->miss.value}, {*next_test}))
          break;
        test = *next_test;
      }
      if (!case_entry) exit = WorkBlockId{seed.control.node.source};
    }
    const auto region = static_cast<std::uint32_t>(region_count),
               first = static_cast<std::uint32_t>(clause_count);
    auto at = root;
    while (true) {
      q.Work();
      Require(tests[at] && !v.proof.blocks[at]);
      const auto& t = *tests[at];
      const bool left = std::get_if<ValueId>(&t.operands[0]) &&
                        std::get<ValueId>(t.operands[0]) == *disc;
      Require(left || (std::get_if<ValueId>(&t.operands[1]) &&
                       std::get<ValueId>(t.operands[1]) == *disc));
      Require(clause_count < v.proof.clauses.size());
      test_clause[at] = static_cast<std::uint32_t>(clause_count);
      v.proof.clauses[clause_count++] = {
          SwitchTest{
              {at}, t.branch, t.comparison, *disc, t.match, t.miss, left},
          g.edges[t.match.value].target};
      v.proof.blocks[at] = SwitchBlockOwner{region, {}};
      auto miss = g.edges[t.miss.value].target.value;
      const auto candidate = ends[miss];
      if (!candidate || !same(t, *tests[*candidate]) ||
          !edge_dom.Dominates({n + t.miss.value}, {*candidate}) ||
          (!std::get_if<ValueId>(&tests[*candidate]->operands[0]) ||
           std::get<ValueId>(tests[*candidate]->operands[0]) != *disc) &&
              (!std::get_if<ValueId>(&tests[*candidate]->operands[1]) ||
               std::get<ValueId>(tests[*candidate]->operands[1]) != *disc)) {
        // A no-match inert corridor leading to the follow has no default body.
        auto end = miss;
        std::size_t steps = 0;
        while ((!exit || end != exit->value) && inert[end] && steps++ < n)
          end = g.edges[g.blocks[end].first_edge.value].target.value;
        Require(steps <= n);
        while (miss != end) {
          q.Work();
          Require(!v.proof.blocks[miss]);
          v.proof.blocks[miss] = SwitchBlockOwner{region, {}};
          miss = g.edges[g.blocks[miss].first_edge.value].target.value;
        }
        if (!exit || end != exit->value) {
          Require(clause_count < v.proof.clauses.size());
          v.proof.clauses[clause_count++] = {{}, WorkBlockId{miss}};
        }
        break;
      }
      while (miss != *candidate) {
        q.Work();
        Require(!v.proof.blocks[miss]);
        v.proof.blocks[miss] = SwitchBlockOwner{region, {}};
        miss = g.edges[g.blocks[miss].first_edge.value].target.value;
      }
      at = *candidate;
    }
    const auto count = static_cast<std::uint32_t>(clause_count - first);
    Require(count >= 2);
    // Empty labels share an entry. The last label alone owns that body.
    for (std::uint32_t c = first; c < first + count; ++c) {
      q.Work();
      auto& clause = v.proof.clauses[c];
      const auto b = clause.body->value;
      if (case_stamp[b] == region + 1)
        v.proof.clauses[case_root[b]].body.reset();
      case_root[b] = c;
      case_stamp[b] = region + 1;
    }
    for (std::uint32_t c = first; c < first + count; ++c) {
      q.Work();
      const auto body = v.proof.clauses[c].body;
      if (!body) continue;
      std::size_t read = 0, size = 1;
      queue[0] = body->value;
      Require(!v.proof.blocks[body->value]);
      v.proof.blocks[body->value] = SwitchBlockOwner{region, c};
      while (read < size) {
        q.Work();
        const auto b = queue[read++];
        const auto& block = g.blocks[b];
        Require(dom.Dominates({root}, {b}));
        for (std::uint8_t e = 0; e < block.edge_count; ++e) {
          q.Work();
          const auto target = g.edges[block.first_edge.value + e].target.value;
          if ((exit && target == exit->value) ||
              case_stamp[target] == region + 1)
            continue;
          if (forest.backedges[block.first_edge.value + e] &&
              dom.Dominates({target}, {root}))
            continue;
          if (v.proof.blocks[target]) {
            Require(v.proof.blocks[target] ==
                    std::optional{SwitchBlockOwner{region, c}});
            continue;
          }
          Require(size < queue.size());
          queue[size++] = target;
          v.proof.blocks[target] = SwitchBlockOwner{region, c};
        }
      }
    }
    v.proof.regions[region_count++] = {{root}, exit, *disc, first, count};
  }
  Require(region_count != 0);
  v.proof.regions.resize(region_count);
  v.proof.clauses.resize(clause_count);
  // Per-entry ownership and all cross-case ports are checked against the
  // ORIGINAL edges. No PC/body ordering or opcode-template permission.
  std::vector<std::optional<std::uint32_t>> entry_clause;
  q.Size(entry_clause, n);
  for (std::uint32_t c = 0; c < clause_count; ++c)
    if (v.proof.clauses[c].body)
      entry_clause[v.proof.clauses[c].body->value] = c;
  std::vector<std::optional<std::uint32_t>> next_clause;
  q.Size(next_clause, clause_count);
  for (std::uint32_t e = 0; e < g.edges.size(); ++e) {
    q.Work();
    const auto& edge = g.edges[e];
    const auto from = v.proof.blocks[edge.source.value],
               to = v.proof.blocks[edge.target.value];
    if (!from) {
      Require(!to || (!to->clause &&
                      v.proof.regions[to->region].entry == edge.target));
      continue;
    }
    if (!from->clause) {
      if (to && to->region == from->region && !to->clause) continue;
      bool match = false;
      std::uint32_t c = v.proof.regions[from->region].first_clause;
      // O(1) test lookup by source branch; default is the final miss target.
      if (tests[edge.source.value] &&
          tests[edge.source.value]->match.value == e) {
        match = true;
        Require(test_clause[edge.source.value].has_value());
        c = *test_clause[edge.source.value];
      } else if (to && to->clause)
        c = *to->clause;
      else
        Require(v.proof.regions[from->region].follow == edge.target);
      v.proof.ports[port_count++] = {
          from->region,
          c,
          {e},
          match || (to && to->region == from->region && to->clause)
              ? SwitchPortKind::kMatch
              : SwitchPortKind::kMiss};
    } else {
      if (to == from) continue;
      const auto& r = v.proof.regions[from->region];
      if (forest.backedges[e] &&
          dom.Dominates({edge.target.value}, {r.entry.value}))
        continue;
      if (r.follow == edge.target)
        v.proof.ports[port_count++] = {
            from->region, *from->clause, {e}, SwitchPortKind::kBreak};
      else {
        Require(to && to->region == from->region &&
                entry_clause[edge.target.value] &&
                to->clause == entry_clause[edge.target.value]);
        auto& next = next_clause[*from->clause];
        Require(!next || *next == *to->clause);
        next = *to->clause;
        v.proof.ports[port_count++] = {
            from->region, *from->clause, {e}, SwitchPortKind::kFallThrough};
      }
    }
  }
  // Default may occur between ordered tests. Its physical place is constrained
  // only by actual fall-through. Otherwise canonical last is equivalent.
  std::vector<std::uint32_t> position, next_body;
  q.Size(position, clause_count);
  q.Size(next_body, clause_count + 1);
  std::vector<SwitchClause> ordered;
  q.Size(ordered, clause_count);
  for (auto& r : v.proof.regions) {
    q.Work();
    const auto first = r.first_clause, end = first + r.clause_count;
    std::optional<std::uint32_t> def;
    if (!v.proof.clauses[end - 1].test) def = end - 1;
    auto insertion = end - 1;
    if (def) {
      std::optional<std::uint32_t> place;
      if (next_clause[*def]) place = *next_clause[*def];
      for (std::uint32_t c = first; c < end - 1; ++c)
        if (next_clause[c] == def) {
          q.Work();
          Require(!place || *place == c + 1);
          place = c + 1;
        }
      if (place) insertion = *place;
    }
    for (std::uint32_t c = first; c < end; ++c) {
      q.Work();
      position[c] = def && c == *def
                        ? insertion
                        : c + (def && c >= insertion && c < *def ? 1U : 0U);
      ordered[position[c]] = v.proof.clauses[c];
    }
    next_body[end] = end;
    for (auto c = end; c-- > first;) {
      q.Work();
      next_body[c] = ordered[c].body ? c : next_body[c + 1];
    }
    for (std::uint32_t c = first; c < end; ++c)
      if (next_clause[c]) {
        q.Work();
        Require(position[*next_clause[c]] == next_body[position[c] + 1]);
      }
  }
  v.proof.clauses = std::move(ordered);
  for (auto& owner : v.proof.blocks) {
    q.Work();
    if (owner && owner->clause) owner->clause = position[*owner->clause];
  }
  v.proof.ports.resize(port_count);
  for (auto& port : v.proof.ports) {
    q.Work();
    port.clause = position[port.clause];
  }
  Require(n <= std::numeric_limits<std::uint32_t>::max() - port_count);
  q.Size(v.nodes, n + port_count);
  q.Size(v.arcs, g.edges.size());
  const ControlGraphView normal(g);
  for (std::uint32_t b = 0; b < n; ++b) {
    q.Work();
    v.nodes[b] = normal.Node({b});
  }
  for (std::uint32_t e = 0; e < g.edges.size(); ++e) {
    q.Work();
    v.arcs[e] = normal.Arc({e});
  }
  q.Size(v.loops.owners, n + port_count);
  v.loops.loops = std::move(forest.loops);
  std::copy(forest.owners.begin(), forest.owners.end(), v.loops.owners.begin());
  for (std::uint32_t p = 0; p < port_count; ++p) {
    q.Work();
    auto& arc = v.arcs[v.proof.ports[p].edge.value];
    const auto id = n + p;
    v.loops.owners[id] = forest.owners[arc.source.value];
    arc.target = {id};
    v.nodes[id] = {{static_cast<std::uint32_t>(g.edges.size())},
                   0,
                   ControlTermination::kAbrupt};
  }
  // Forest entries are selected by reachability once. An already reachable
  // follow is never emitted a second time as an independent root.
  std::vector<std::uint8_t> reached;
  q.Size(reached, v.nodes.size());
  std::vector<std::uint32_t> reach_stack;
  q.Size(reach_stack, v.nodes.size());
  q.Size(v.proof.entries, n);
  std::size_t entries = 0;
  const auto enter = [&](std::uint32_t b) {
    if (reached[b]) return;
    q.Work();
    v.proof.entries[entries++] = {{b}, forest.owners[b]};
    std::size_t size = 1;
    reach_stack[0] = b;
    reached[b] = 1;
    while (size) {
      q.Work();
      const auto x = reach_stack[--size];
      const auto& node = v.nodes[x];
      for (std::uint8_t e = 0; e < node.arc_count; ++e) {
        q.Work();
        const auto to = v.arcs[node.first_arc.value + e].target.value;
        if (!reached[to]) {
          reached[to] = 1;
          reach_stack[size++] = to;
        }
      }
    }
  };
  enter(0);
  for (const auto& r : v.proof.regions) {
    for (std::uint32_t c = r.first_clause; c < r.first_clause + r.clause_count;
         ++c)
      if (v.proof.clauses[c].body) enter(v.proof.clauses[c].body->value);
    if (r.follow) enter(r.follow->value);
  }
  for (auto x : reached) {
    q.Work();
    Require(x);
  }
  v.proof.entries.resize(entries);
  v.proof.allocated_bytes =
      sizeof(SwitchRegions) +
      v.proof.regions.capacity() * sizeof(SwitchRegion) +
      v.proof.clauses.capacity() * sizeof(SwitchClause) +
      v.proof.ports.capacity() * sizeof(SwitchPortEvidence) +
      v.proof.blocks.capacity() * sizeof(std::optional<SwitchBlockOwner>) +
      v.proof.entries.capacity() * sizeof(ScopedControlEntry);
  return v;
}
ControlGraphView Graph(const ValueProgram& g, const View& v) {
  return ControlGraphView(
      ControlProjection{g.revision, g.cfg_revision, v.nodes, v.arcs});
}
}  // namespace
ControlPlan BuildSwitchControl(const ValueProgram& g,
                               const ControlFailure& failure, ControlQuota& q) {
  if (failure.kind != Error::kCrossEntry) throw failure;
  try {
    auto view = Derive(g, failure, q);
    ControlPlan plan;
    if (view.loops.loops.empty()) {
      std::vector<ControlId> roots;
      q.Size(roots, view.proof.entries.size());
      for (std::size_t i = 0; i < roots.size(); ++i)
        roots[i] = {view.proof.entries[i].node.value};
      plan = BuildControlSkeleton(Graph(g, view), roots, q);
    } else
      plan = BuildLoopControl(Graph(g, view), view.proof.entries,
                              std::move(view.loops), q);
    for (std::uint32_t p = 0; p < view.proof.ports.size(); ++p) {
      auto& node = plan.nodes[g.blocks.size() + p];
      node.synthetic = SwitchPort{p};
      node.block = g.edges[view.proof.ports[p].edge.value].source;
    }
    plan.view = ControlViewKind::kNormal;
    q.Admit(1, sizeof(SwitchRegions) +
                   sizeof(std::shared_ptr<const SwitchRegions>) +
                   alignof(std::max_align_t));
    plan.allocated_bytes += view.proof.allocated_bytes;
    plan.switches = std::make_shared<SwitchRegions>(std::move(view.proof));
    return plan;
  } catch (Error e) {
    if (e == Error::kResourceLimit || e == Error::kWorkLimit) throw;
    throw failure;
  }
}
void VerifySwitchControl(const ValueProgram& g, const ControlPlan& p,
                         ControlQuota& q) {
  Require(p.switches && p.forwarding.empty());
  auto view = Derive(g, p.switches->seed, q);
  const auto& a = *p.switches;
  const auto& b = view.proof;
  Require(a.regions == b.regions && a.clauses == b.clauses &&
          a.ports == b.ports && a.blocks == b.blocks &&
          a.entries.size() == b.entries.size());
  for (std::size_t i = 0; i < a.entries.size(); ++i)
    Require(a.entries[i].node == b.entries[i].node &&
            a.entries[i].scope == b.entries[i].scope);
  ControlPlan shadow;
  q.Size(shadow.nodes, p.nodes.size());
  q.Size(shadow.edges, p.edges.size());
  q.Size(shadow.loops, p.loops.size());
  shadow.view = ControlViewKind::kProjection;
  shadow.revision = p.revision;
  shadow.cfg_revision = p.cfg_revision;
  shadow.entry = p.entry;
  std::copy(p.nodes.begin(), p.nodes.end(), shadow.nodes.begin());
  std::copy(p.edges.begin(), p.edges.end(), shadow.edges.begin());
  std::copy(p.loops.begin(), p.loops.end(), shadow.loops.begin());
  Require(shadow.nodes.size() >= view.nodes.size());
  for (std::uint32_t i = 0; i < a.ports.size(); ++i) {
    auto& node = shadow.nodes[g.blocks.size() + i];
    const auto* port = std::get_if<SwitchPort>(&node.synthetic);
    Require(port && port->port == i &&
            node.block == g.edges[a.ports[i].edge.value].source);
    node.synthetic = {};
    node.block = {static_cast<std::uint32_t>(g.blocks.size() + i)};
  }
  VerifyControlRegions(Graph(g, view), view.proof.entries, shadow, q);
}
}  // namespace abcd::source
