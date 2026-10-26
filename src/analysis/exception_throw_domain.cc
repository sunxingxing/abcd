#include "abcd/analysis/exception_throw_domain.h"

#include <algorithm>
#include <tuple>
#include <map>
#include <bit>
#include "register_value_rule.h"

namespace abcd::analysis {
namespace {
struct Guard {
  SsaValueId value;
  ExceptionNodeId comparison, branch;
  std::uint8_t successor;
  std::uint32_t begin, end;
};
}
std::pmr::vector<ExceptionThrowDomain> DeriveThrowDomains(
    const ExceptionFlow& flow, const ExceptionSsa& ssa, RawEventArena& arena,
    GuardedCatchPayloads* certificate = nullptr) {
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
  const auto dominates = [&](std::uint32_t a, std::uint32_t b) {
    arena.Step();
    return a < total && b < total && dom->nodes[a].begin && dom->nodes[b].begin &&
        dom->nodes[a].begin <= dom->nodes[b].begin && dom->nodes[b].begin < dom->nodes[a].end;
  };
  if (certificate) {
    const auto reject = [](ExceptionThrowDomainError error) { throw ExceptionThrowDomainFailure{error}; };
    const auto same_slot = [](ScalarSlot a, ScalarSlot b) {
      if (const auto* reg = std::get_if<abc::RegisterId>(&a)) {
        const auto* other = std::get_if<abc::RegisterId>(&b); return other && *reg == *other;
      }
      return std::holds_alternative<Accumulator>(b);
    };
    certificate->definitions.reserve(nv);
    std::pmr::vector<std::uint32_t> predecessors(nn, 0, memory), phi_inputs(nv, 0, memory), read_counts(nn, 0, memory);
    for (std::uint32_t n = 0; n < nn; ++n) {
      arena.Step(); if (!dom->nodes[n].begin) continue;
      for (std::uint8_t e = 0; e < flow.nodes[n].successor_count; ++e) {
        arena.Step(); ++predecessors[flow.nodes[n].successors[e].target.value];
      }
    }
    for (std::uint32_t v = 0; v < nv; ++v) {
      arena.Step(); const auto& value = ssa.Values()[v]; const auto site = definition(v);
      ThrowValueDefinition evidence{{v}, {}, {}, std::holds_alternative<PhiValue>(value.origin)};
      if (site) {
        if (*site >= nn || !dom->nodes[*site].begin) reject(ExceptionThrowDomainError::kActivation);
        evidence.site = ExceptionNodeId{*site};
        if (!evidence.phi) {
          const auto& node = flow.nodes[*site];
          if (ssa.Write({*site}) != SsaValueId{v}) reject(ExceptionThrowDomainError::kActivation);
          if (node.phase == ExceptionPhase::kCommit) {
            const auto& write = flow.instructions[node.instruction.value].effects.normal_registers.write;
            if (!write || !same_slot(*write, value.slot)) reject(ExceptionThrowDomainError::kActivation);
          } else if (node.phase != ExceptionPhase::kCatch || !std::holds_alternative<Accumulator>(value.slot)) {
            reject(ExceptionThrowDomainError::kActivation);
          }
        }
      }
      if (copies[v] != v) evidence.copy = SsaValueId{copies[v]};
      certificate->definitions.push_back(evidence);
    }
    certificate->reads.reserve(ssa.AllReads().size());
    for (const auto& read : ssa.AllReads()) {
      arena.Step(); const auto n = read.instruction.value;
      if (n >= nn || read.value.value >= nv || flow.nodes[n].phase != ExceptionPhase::kBefore ||
          !dom->nodes[n].begin || read.ordinal != read_counts[n]++) reject(ExceptionThrowDomainError::kActivation);
      const auto& effect = flow.instructions[flow.nodes[n].instruction.value].effects.normal_registers;
      std::optional<ScalarSlot> expected;
      if (read.ordinal < effect.read_count) expected = effect.reads[read.ordinal];
      else if (effect.range && read.ordinal - effect.read_count < effect.range->count)
        expected = abc::RegisterId{effect.range->first.value + read.ordinal - effect.read_count};
      const auto site = definition(read.value.value);
      if (!expected || !same_slot(*expected, ssa.Values()[read.value.value].slot) ||
          (site && !dominates(*site, n))) reject(ExceptionThrowDomainError::kActivation);
      certificate->reads.push_back(read);
    }
    for (std::uint32_t n = 0; n < nn; ++n) {
      arena.Step(); if (!dom->nodes[n].begin || flow.nodes[n].phase != ExceptionPhase::kBefore) continue;
      const auto& effect = flow.instructions[flow.nodes[n].instruction.value].effects.normal_registers;
      if (read_counts[n] != effect.read_count + (effect.range ? effect.range->count : 0))
        reject(ExceptionThrowDomainError::kActivation);
    }
    certificate->phis.reserve(ssa.AllInputs().size());
    std::optional<std::tuple<SsaValueId, std::optional<ExceptionNodeId>, std::uint8_t>> previous_phi;
    for (const auto& input : ssa.AllInputs()) {
      arena.Step();
      if (input.phi.value >= nv || input.value.value >= nv) reject(ExceptionThrowDomainError::kPhi);
      const auto* phi = std::get_if<PhiValue>(&ssa.Values()[input.phi.value].origin);
      const auto* edge = std::get_if<SsaEdge>(&input.edge);
      if (!phi || phi->block.value >= nn || !same_slot(ssa.Values()[input.phi.value].slot, ssa.Values()[input.value.value].slot))
        reject(ExceptionThrowDomainError::kPhi);
      if (!edge) {
        if (phi->block.value != 0 || !std::holds_alternative<MethodEntry>(ssa.Values()[input.value.value].origin))
          reject(ExceptionThrowDomainError::kPhi);
        certificate->phis.push_back({input.phi, input.value, {}, 0});
      } else {
        if (edge->source.value >= nn || edge->successor >= flow.nodes[edge->source.value].successor_count ||
            !dom->nodes[edge->source.value].begin ||
            flow.nodes[edge->source.value].successors[edge->successor].target.value != phi->block.value)
          reject(ExceptionThrowDomainError::kPhi);
        const auto site = definition(input.value.value);
        if (site && !dominates(*site, edge->source.value)) reject(ExceptionThrowDomainError::kActivation);
        certificate->phis.push_back({input.phi, input.value, ExceptionNodeId{edge->source.value}, edge->successor});
      }
      const auto& row = certificate->phis.back();
      const auto key = std::tuple{row.phi,row.source,row.successor};
      if (previous_phi && key <= *previous_phi) reject(ExceptionThrowDomainError::kPhi);
      previous_phi = key;
      ++phi_inputs[input.phi.value];
    }
    for (std::uint32_t v = 0; v < nv; ++v) {
      arena.Step(); if (const auto* phi = std::get_if<PhiValue>(&ssa.Values()[v].origin))
        if (phi_inputs[v] != predecessors[phi->block.value] + (phi->block.value == 0 ? 1 : 0))
          reject(ExceptionThrowDomainError::kCoverage);
    }
    for (std::uint32_t n = 0; n < nn; ++n) {
      arena.Step(); if (!dom->nodes[n].begin) continue;
      const auto& node = flow.nodes[n];
      for (std::uint8_t e = 0; e < node.successor_count; ++e) {
        arena.Step(); const auto& arc = node.successors[e];
        if (arc.kind != ExceptionArcKind::kException) continue;
        if (node.phase != ExceptionPhase::kBefore || flow.nodes[arc.target.value].phase != ExceptionPhase::kCatch || !node.exception)
          reject(ExceptionThrowDomainError::kCoverage);
        std::optional<SsaValueId> payload;
        if (std::holds_alternative<ThrownValue>(node.exception->exception)) {
          const auto reads = ssa.Reads({n});
          if (reads.size() != 1) reject(ExceptionThrowDomainError::kCoverage);
          payload = reads[0].value;
        }
        certificate->catches.push_back({{n}, arc.target, e, payload});
      }
    }
  }
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
    if (def && (*def >= nn || (!certificate && component[*def] == component[n]))) continue;
    if (certificate && def && !dominates(*def, *comparison)) continue;
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
    if (g.value == value && begin >= g.begin && begin < g.end) {
      const auto def = definition(value.value);
      // In valid SSA a definition dominates every use. If it executes again
      // before this throw, the same selected edge must execute again as well:
      // otherwise prefixing that def-to-use path with its first entry path
      // contradicts edge dominance. Copies stop at phi/Catch definitions.
      if (certificate && def && (!dominates(*def, n) ||
          dominates(nn + offsets[g.branch.value] + g.successor, *def))) continue;
      out.push_back({{n}, reads[0].value, value, g.comparison, g.branch, g.successor,
          certificate && def ? std::optional{ExceptionNodeId{*def}} : std::nullopt});
    }
  }
  if (certificate) {
    struct KeyLess {
      RawEventArena* arena;
      bool operator()(const std::pair<SsaValueId,ThrowFactUse>& a,
                      const std::pair<SsaValueId,ThrowFactUse>& b) const { arena->Step();return a<b; }
    };
    std::pmr::map<std::pair<SsaValueId,ThrowFactUse>,std::uint32_t,KeyLess> queries(KeyLess{&arena},memory);
    std::pmr::vector<std::uint32_t> phi_begin(nv+1,0,memory), catch_begin(nn+1,0,memory);
    for(const auto& row:certificate->phis){arena.Step();++phi_begin[row.phi.value+1];}
    for(std::size_t i=1;i<phi_begin.size();++i){arena.Step();phi_begin[i]+=phi_begin[i-1];}
    // Catch coverage is indexed once; do not scan all throwing sites for each query.
    for(const auto& row:certificate->catches){arena.Step();++catch_begin[row.destination.value+1];}
    for(std::size_t i=1;i<catch_begin.size();++i){arena.Step();catch_begin[i]+=catch_begin[i-1];}
    std::pmr::vector<std::uint32_t> catch_cursor(catch_begin,memory), catch_sources(certificate->catches.size(),0,memory);
    for(std::uint32_t i=0;i<certificate->catches.size();++i){arena.Step();catch_sources[catch_cursor[certificate->catches[i].destination.value]++]=i;}
    const auto query=[&](SsaValueId value,ThrowFactUse use) {
      arena.Step();const auto root=SsaValueId{roots[value.value]};const auto key=std::pair{root,use};
      if(const auto found=queries.find(key);found!=queries.end())return found->second;
      if(certificate->facts.size()>=UINT32_MAX)throw ExceptionThrowDomainFailure{ExceptionThrowDomainError::kResourceLimit};
      const auto id=static_cast<std::uint32_t>(certificate->facts.size());
      queries.emplace(key,id);certificate->facts.push_back({root,use});return id;
    };
    // Roots are occurrence queries, not globally narrowed SSA values. A phi
    // fact needs ALL predecessor occurrences; a Catch needs ALL incoming
    // exceptions. Unsupported/unseeded cycles stay unknown at the least fixed
    // point. Runtime pending payloads are left to the solver's owner premise.
    for(std::uint32_t n=0;n<nn;++n) {
      arena.Step();if(!dom->nodes[n].begin || !flow.nodes[n].exception ||
          !std::holds_alternative<ThrownValue>(flow.nodes[n].exception->exception))continue;
      const auto reads=ssa.Reads({n});if(reads.size()!=1)fail();
      const auto fact=query(reads[0].value,{{n},{}});
      certificate->constraints.push_back({{n},reads[0].value,fact});
    }
    for(std::uint32_t id=0;id<certificate->facts.size();++id) {
      arena.Step();const auto value=certificate->facts[id].value;const auto use=certificate->facts[id].use;
      const auto def=definition(value.value);
      const auto point=use.successor ? nn+offsets[use.node.value]+*use.successor : use.node.value;
      const auto begin=dom->nodes[point].begin;
      const auto after=std::upper_bound(guards.begin(),guards.end(),std::tuple(value,begin),[&](const auto& key,const auto& guard){arena.Step();return key<std::tuple(guard.value,guard.begin);});
      if(after!=guards.begin()) {
        const auto& guard=*std::prev(after);
        if(guard.value==value && begin>=guard.begin && begin<guard.end &&
            (!def || (dominates(*def,point) && !dominates(nn+offsets[guard.branch.value]+guard.successor,*def)))) {
          auto& fact=certificate->facts[id];fact.kind=ThrowFactKind::kGuard;fact.non_hole=true;
          fact.guard=ExceptionThrowDomain{use.node,value,value,guard.comparison,guard.branch,guard.successor,
              def?std::optional{ExceptionNodeId{*def}}:std::nullopt};continue;
        }
      }
      if(!def)continue;
      const auto& origin=ssa.Values()[value.value].origin;
      if ((std::holds_alternative<PhiValue>(origin) || flow.nodes[*def].phase==ExceptionPhase::kCatch) && (use.successor || use.node.value!=*def)) {
        certificate->facts[id].kind=ThrowFactKind::kTransport;
        const auto input=query(value,{{*def},{}});certificate->inputs.push_back({id,input,{}});continue;
      }
      if(std::holds_alternative<PhiValue>(origin)) {
        certificate->facts[id].kind=ThrowFactKind::kPhi;
        for(auto i=phi_begin[value.value];i<phi_begin[value.value+1];++i) {
          arena.Step();const auto& row=certificate->phis[i];
          if(!row.source){certificate->facts[id].kind=ThrowFactKind::kUnknown;continue;}
          const auto input=query(row.value,{*row.source,row.successor});
          certificate->inputs.push_back({id,input,i});
        }
      } else if(flow.nodes[*def].phase==ExceptionPhase::kCatch) {
        certificate->facts[id].kind=ThrowFactKind::kCatch;
        for(auto i=catch_begin[*def];i<catch_begin[*def+1];++i) {
          arena.Step();const auto index=catch_sources[i];const auto& row=certificate->catches[index];
          if(!row.payload){certificate->facts[id].kind=ThrowFactKind::kUnknown;continue;}
          const auto input=query(*row.payload,{row.source,{}});
          certificate->inputs.push_back({id,input,index});
        }
      } else {
        const auto described=detail::DescribeRegisterValue(flow.instructions[flow.nodes[*def].instruction.value],
            CallResultValues::kUnknown,FrameReceiver::kUnknown,ArithmeticSemantics::kPinnedRuntime,GlobalReadValues::kUnknown);
        if(const auto* rule=std::get_if<detail::ValueRule>(&described);rule && rule->kind==detail::ValueRuleKind::kFixed &&
            !rule->seed.IsEmpty() && !rule->seed.Contains(VmValueKind::kHole)) {
          certificate->facts[id].kind=ThrowFactKind::kFixed;certificate->facts[id].non_hole=true;
        }
      }
    }
    const auto count=certificate->facts.size();
    std::pmr::vector<std::uint32_t> needed(count,0,memory), starts(count+1,0,memory), queue(memory);
    for(const auto& edge:certificate->inputs){arena.Step();++needed[edge.fact];++starts[edge.input+1];}
    for(std::size_t i=1;i<starts.size();++i){arena.Step();starts[i]+=starts[i-1];}
    std::pmr::vector<std::uint32_t> cursor(starts,memory), targets(certificate->inputs.size(),0,memory);
    for(const auto& edge:certificate->inputs){arena.Step();targets[cursor[edge.input]++]=edge.fact;}
    for(std::uint32_t id=0;id<count;++id){arena.Step();if(certificate->facts[id].non_hole)queue.push_back(id);}
    for(std::size_t i=0;i<queue.size();++i)for(auto e=starts[queue[i]];e<starts[queue[i]+1];++e) {
      arena.Step();const auto target=targets[e];auto& fact=certificate->facts[target];
      if(--needed[target]==0 && (fact.kind==ThrowFactKind::kPhi || fact.kind==ThrowFactKind::kCatch || fact.kind==ThrowFactKind::kTransport)) {
        fact.non_hole=true;queue.push_back(target);
      }
    }
    std::size_t kept=0;
    for(const auto& constraint:certificate->constraints){arena.Step();if(certificate->facts[constraint.fact].non_hole)certificate->constraints[kept++]=constraint;}
    certificate->constraints.resize(kept);
  }
  return out;
}
std::pmr::vector<ExceptionThrowDomain> DeriveExceptionThrowDomains(
    const ExceptionFlow& flow, const ExceptionSsa& ssa, RawEventArena& arena) {
  return DeriveThrowDomains(flow, ssa, arena);
}
void VerifyExceptionThrowDomains(const ExceptionFlow& flow, const ExceptionSsa& ssa,
    std::span<const ExceptionThrowDomain> supplied, RawEventArena& arena) {
  const auto expected = DeriveExceptionThrowDomains(flow, ssa, arena);
  if (expected.size() != supplied.size()) throw RawEventFailure{RawEventError::kCorrespondence};
  for (std::size_t i = 0; i < expected.size(); ++i) {
    arena.Step(); if (expected[i] != supplied[i]) throw RawEventFailure{RawEventError::kCorrespondence};
  }
}
GuardedCatchPayloads DeriveGuardedCatchPayloads(
    const ExceptionFlow& flow, const ExceptionSsa& ssa, RawEventArena& arena) {
  // Bound sparse proof expansion by input work, including independent
  // verification. Dense occurrence relations refuse work rather than turn
  // this shared proof into a per-value whole-CFG traversal.
  const auto size=std::uint64_t(flow.nodes.size())+ssa.Values().size()+ssa.AllReads().size()+ssa.AllInputs().size()+1;
  constexpr std::uint64_t kWorkPerInput=4096;
  if(size>UINT32_MAX)throw ExceptionThrowDomainFailure{ExceptionThrowDomainError::kResourceLimit};
  if(arena.Work()==0)arena.LimitWork(std::min(arena.RemainingWork(),kWorkPerInput*size*(1+std::bit_width(size))));
  GuardedCatchPayloads result{arena.Resource()};
  result.throws = DeriveThrowDomains(flow, ssa, arena, &result);
  return result;
}
void VerifyGuardedCatchPayloads(const ExceptionFlow& flow, const ExceptionSsa& ssa,
    const GuardedCatchPayloads& supplied, RawEventArena& arena) {
  const auto expected = DeriveGuardedCatchPayloads(flow, ssa, arena);
  const auto check = [&](const auto& a, const auto& b, ExceptionThrowDomainError error) {
    if (a.size() != b.size()) throw ExceptionThrowDomainFailure{ExceptionThrowDomainError::kCoverage};
    for (std::size_t i = 0; i < a.size(); ++i) {
      arena.Step(); if (a[i] != b[i]) throw ExceptionThrowDomainFailure{error};
    }
  };
  check(supplied.throws, expected.throws, ExceptionThrowDomainError::kGuard);
  check(supplied.definitions, expected.definitions, ExceptionThrowDomainError::kActivation);
  check(supplied.phis, expected.phis, ExceptionThrowDomainError::kPhi);
  if (supplied.reads.size() != expected.reads.size()) throw ExceptionThrowDomainFailure{ExceptionThrowDomainError::kCoverage};
  for (std::size_t i=0;i<supplied.reads.size();++i) {
    arena.Step(); const auto& a=supplied.reads[i]; const auto& b=expected.reads[i];
    if(a.instruction!=b.instruction || a.ordinal!=b.ordinal || a.value!=b.value)
      throw ExceptionThrowDomainFailure{ExceptionThrowDomainError::kActivation};
  }
  check(supplied.catches, expected.catches, ExceptionThrowDomainError::kCoverage);
  check(supplied.facts, expected.facts, ExceptionThrowDomainError::kGuard);
  check(supplied.inputs, expected.inputs, ExceptionThrowDomainError::kPhi);
  check(supplied.constraints, expected.constraints, ExceptionThrowDomainError::kCoverage);
}
std::uint64_t VerifyGuardedCatchEvidence(const GuardedCatchEvidence& evidence, SemanticBudget budget) {
  if (!evidence.flow || !evidence.ssa) throw ExceptionThrowDomainFailure{ExceptionThrowDomainError::kInput};
  RawEventArena arena{budget};
  VerifyGuardedCatchPayloads(*evidence.flow, *evidence.ssa, evidence.proof, arena);
  return arena.Work();
}
}  // namespace abcd::analysis
