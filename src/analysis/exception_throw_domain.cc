#include "abcd/analysis/exception_throw_domain.h"

#include <algorithm>
#include <tuple>

namespace abcd::analysis {
namespace {
struct Guard {
  SsaValueId value;
  ExceptionNodeId comparison, branch;
  std::uint8_t successor;
  std::uint32_t begin, end;
};
}
std::pmr::vector<ExceptionThrowDomain> DeriveExceptionThrowDomains(
    const ExceptionFlow& flow, const ExceptionSsa& ssa, RawEventArena& arena) {
  const auto fail = [] { throw RawEventFailure{RawEventError::kCorrespondence}; };
  auto* memory = arena.Resource();
  const auto nn = flow.nodes.size(), nv = ssa.Values().size();
  if (!nn || nn > UINT32_MAX / 3 || nv > UINT32_MAX)
    throw RawEventFailure{RawEventError::kResourceLimit};
  std::pmr::vector<std::optional<ExceptionNodeId>> before(flow.instructions.size(), {}, memory);
  std::pmr::vector<std::uint32_t> offsets(nn + 1, 0, memory);
  for (std::size_t n = 0; n < nn; ++n) {
    arena.Step(); const auto& node = flow.nodes[n];
    if (node.instruction.value >= before.size() || node.successor_count > node.successors.size()) fail();
    if (node.phase == ExceptionPhase::kBefore) {
      if (before[node.instruction.value]) fail();
      before[node.instruction.value] = ExceptionNodeId{static_cast<std::uint32_t>(n)};
    }
    offsets[n + 1] = offsets[n] + node.successor_count;
  }
  const auto ne = offsets.back();
  const auto total = nn + ne;
  // Split every original edge for genuine edge dominance (a successor with
  // another predecessor is NOT sufficient). All exception/Catch arcs remain.
  std::pmr::vector<std::uint32_t> start(total + 1, 0, memory);
  std::pmr::vector<BlockId> edges(memory);
  edges.reserve(2 * ne);
  for (std::size_t n = 0; n < nn; ++n) {
    arena.Step(); start[n] = static_cast<std::uint32_t>(edges.size());
    for (std::uint8_t e = 0; e < flow.nodes[n].successor_count; ++e) {
      arena.Step(); const auto target = flow.nodes[n].successors[e].target;
      if (target.value >= nn) fail();
      edges.push_back({static_cast<std::uint32_t>(nn + offsets[n] + e)});
    }
  }
  for (const auto& node : flow.nodes) for (std::uint8_t e = 0; e < node.successor_count; ++e) {
    arena.Step(); start[nn + (edges.size() - ne)] = static_cast<std::uint32_t>(edges.size());
    edges.push_back({node.successors[e].target.value});
  }
  start.back() = static_cast<std::uint32_t>(edges.size());
  const auto result = BuildDominators({start, edges}, {}, {arena.Available(), arena.RemainingWork()});
  const auto* dom = std::get_if<DominatorTree>(&result);
  if (!dom) throw RawEventFailure{RawEventError::kResourceLimit};
  arena.Observe(dom->work.allocated_bytes); arena.Step(dom->work.steps);
  arena.Retain(dom->nodes.capacity() * sizeof(DominatorNode));
  // Iterative Kosaraju, shared by every candidate. Re-entering the compared
  // definition can invalidate a path fact even though a historical edge still
  // dominates the throw. SCC equality conservatively refuses that activation.
  std::pmr::vector<std::uint32_t> reverse_start(total + 1, 0, memory), reverse(edges.size(), 0, memory);
  for (const auto e : edges) { arena.Step(); ++reverse_start[e.value + 1]; }
  for (std::size_t i = 1; i < reverse_start.size(); ++i) { arena.Step(); reverse_start[i] += reverse_start[i - 1]; }
  std::pmr::vector<std::uint32_t> cursor(reverse_start, memory);
  for (std::uint32_t n = 0; n < total; ++n) for (auto e = start[n]; e < start[n + 1]; ++e) {
    arena.Step(); reverse[cursor[edges[e].value]++] = n;
  }
  std::pmr::vector<std::uint8_t> seen(total, 0, memory);
  std::pmr::vector<std::uint32_t> order(memory), stack(memory), component(total, UINT32_MAX, memory);
  cursor.assign(start.begin(), start.end());
  for (std::uint32_t root = 0; root < total; ++root) {
    arena.Step(); if (seen[root]) continue; seen[root] = 1; stack.push_back(root);
    while (!stack.empty()) {
      arena.Step(); const auto n = stack.back();
      if (cursor[n] == start[n + 1]) { order.push_back(n); stack.pop_back(); continue; }
      const auto to = edges[cursor[n]++].value;
      if (!seen[to]) { seen[to] = 1; stack.push_back(to); }
    }
  }
  std::uint32_t number = 0;
  for (auto it = order.rbegin(); it != order.rend(); ++it) {
    arena.Step(); if (component[*it] != UINT32_MAX) continue;
    component[*it] = number; stack.push_back(*it);
    while (!stack.empty()) {
      arena.Step(); const auto n = stack.back(); stack.pop_back();
      for (auto e = reverse_start[n]; e < reverse_start[n + 1]; ++e) {
        arena.Step(); const auto to = reverse[e];
        if (component[to] == UINT32_MAX) { component[to] = number; stack.push_back(to); }
      }
    }
    ++number;
  }
  // Canonicalize only semantic scalar copies; no aliasing across distinct
  // phis, captures, heap reads or activations is invented.
  std::pmr::vector<std::uint32_t> copies(nv, 0, memory), roots(nv, UINT32_MAX, memory);
  for (std::uint32_t v = 0; v < nv; ++v) {
    arena.Step(); copies[v] = v;
    const auto* origin = std::get_if<InstructionValue>(&ssa.Values()[v].origin);
    if (!origin) continue;
    if (origin->instruction.value >= nn) fail();
    const auto& node = flow.nodes[origin->instruction.value];
    if (node.phase != ExceptionPhase::kCommit) continue;
    const auto* assignment = std::get_if<ScalarAssignment>(&flow.instructions[node.instruction.value].operation);
    if (!assignment) continue;
    const auto* value = std::get_if<ScalarValue>(&assignment->expression);
    if (!value || (!std::holds_alternative<Accumulator>(*value) && !std::holds_alternative<abc::RegisterId>(*value))) continue;
    if (!before[node.instruction.value]) fail();
    const auto reads = ssa.Reads(*before[node.instruction.value]); arena.Step();
    if (reads.size() != 1 || reads[0].value.value >= nv) fail();
    copies[v] = reads[0].value.value;
  }
  seen.assign(nv, 0);
  for (std::uint32_t v = 0; v < nv; ++v) {
    arena.Step(); auto current = v;
    while (roots[current] == UINT32_MAX && !seen[current]) {
      arena.Step(); seen[current] = 1; stack.push_back(current);
      if (copies[current] == current) { roots[current] = current; break; }
      current = copies[current];
    }
    if (roots[current] == UINT32_MAX) fail();
    const auto root = roots[current];
    while (!stack.empty()) { arena.Step(); roots[stack.back()] = root; stack.pop_back(); }
  }
  const auto definition = [&](std::uint32_t v) -> std::optional<std::uint32_t> {
    const auto& origin = ssa.Values()[v].origin;
    if (const auto* site = std::get_if<InstructionValue>(&origin)) return site->instruction.value;
    if (const auto* phi = std::get_if<PhiValue>(&origin)) return phi->block.value;
    return {};
  };
  const auto hole = [&](std::uint32_t v) {
    const auto site = definition(v);
    return site && *site < nn && flow.nodes[*site].phase == ExceptionPhase::kCommit &&
        std::holds_alternative<LoadHole>(flow.instructions[flow.nodes[*site].instruction.value].operation);
  };
  std::pmr::vector<Guard> guards(memory);
  for (std::uint32_t n = 0; n < nn; ++n) {
    arena.Step(); const auto& node = flow.nodes[n];
    if (node.phase != ExceptionPhase::kCommit || !dom->nodes[n].begin) continue;
    const auto* branch = std::get_if<BranchOperation>(&flow.instructions[node.instruction.value].operation);
    if (!branch) continue;
    bool taken_true;
    switch (branch->predicate.op) {
      case UnaryOperator::kTrueOrNonzeroNumber: case UnaryOperator::kToBoolean: taken_true = true; break;
      case UnaryOperator::kFalseOrZero: case UnaryOperator::kLogicalNot: taken_true = false; break;
      default: continue;
    }
    const auto reads = ssa.Reads(*before[node.instruction.value]); arena.Step();
    if (reads.size() != 1 || reads[0].value.value >= nv) fail();
    const auto comparison = definition(roots[reads[0].value.value]);
    if (!comparison || *comparison >= nn) continue;
    const auto& c = flow.nodes[*comparison];
    if (c.phase != ExceptionPhase::kCommit) continue;
    const auto* assignment = std::get_if<ScalarAssignment>(&flow.instructions[c.instruction.value].operation);
    if (!assignment) continue;
    const auto* equal = std::get_if<BinaryExpression>(&assignment->expression);
    if (!equal) continue;
    bool equality;
    switch (equal->op) {
      case BinaryOperator::kEqual: case BinaryOperator::kStrictEqual: equality = true; break;
      case BinaryOperator::kNotEqual: case BinaryOperator::kStrictNotEqual: equality = false; break;
      default: continue;
    }
    const auto operands = ssa.Reads(*before[c.instruction.value]); arena.Step();
    if (operands.size() != 2) continue;
    const auto left = roots[operands[0].value.value], right = roots[operands[1].value.value];
    const bool a = hole(left), b = hole(right);
    if (a == b) continue;
    const auto value = a ? right : left;
    const auto def = definition(value);
    if (def && (*def >= nn || component[*def] == component[n])) continue;
    for (std::uint8_t e = 0; e < node.successor_count; ++e) {
      arena.Step(); const auto kind = node.successors[e].kind;
      if (kind != ExceptionArcKind::kTaken && kind != ExceptionArcKind::kFallthrough) continue;
      const auto true_result = kind == ExceptionArcKind::kTaken ? taken_true : !taken_true;
      if (true_result == equality) continue;
      const auto& interval = dom->nodes[nn + offsets[n] + e];
      if (interval.begin) guards.push_back({{value}, {*comparison}, {n}, e, interval.begin, interval.end});
    }
  }
  std::sort(guards.begin(), guards.end(), [&](const auto& a, const auto& b) {
    arena.Step(); return std::tuple(a.value, a.begin) < std::tuple(b.value, b.begin);
  });
  std::size_t kept = 0;
  for (const auto g : guards) {
    arena.Step(); if (kept && guards[kept - 1].value == g.value && g.begin < guards[kept - 1].end) continue;
    guards[kept++] = g;
  }
  guards.resize(kept);
  std::pmr::vector<ExceptionThrowDomain> out(memory);
  for (std::uint32_t n = 0; n < nn; ++n) {
    arena.Step(); const auto& node = flow.nodes[n];
    if (node.phase != ExceptionPhase::kBefore || !dom->nodes[n].begin || !node.exception ||
        !std::holds_alternative<ThrownValue>(node.exception->exception)) continue;
    const auto reads = ssa.Reads({n}); arena.Step(); if (reads.size() != 1) fail();
    const auto value = SsaValueId{roots[reads[0].value.value]};
    const auto begin = dom->nodes[n].begin;
    const auto after = std::upper_bound(guards.begin(), guards.end(), std::tuple(value, begin), [&](const auto& key, const auto& g) {
      arena.Step(); return key < std::tuple(g.value, g.begin);
    });
    if (after == guards.begin()) continue;
    const auto& g = *std::prev(after);
    if (g.value == value && begin >= g.begin && begin < g.end)
      out.push_back({{n}, reads[0].value, value, g.comparison, g.branch, g.successor});
  }
  return out;
}
void VerifyExceptionThrowDomains(const ExceptionFlow& flow, const ExceptionSsa& ssa,
    std::span<const ExceptionThrowDomain> supplied, RawEventArena& arena) {
  const auto expected = DeriveExceptionThrowDomains(flow, ssa, arena);
  if (expected.size() != supplied.size()) throw RawEventFailure{RawEventError::kCorrespondence};
  for (std::size_t i = 0; i < expected.size(); ++i) {
    arena.Step(); if (expected[i] != supplied[i]) throw RawEventFailure{RawEventError::kCorrespondence};
  }
}
}  // namespace abcd::analysis
