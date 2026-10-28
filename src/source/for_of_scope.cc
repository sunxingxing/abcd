#include "for_of_scope.h"

#include <algorithm>
#include <bit>
#include <deque>
#include <map>
#include <stdexcept>

#include "../analysis/idf_scratch.h"
#include "abcd/analysis/dominators.h"
#include "completion_ports.h"
#include "for_of_iteration.h"
namespace abcd::source {
namespace {
using namespace analysis;
using Error = SinglePassError;
constexpr std::uint8_t kUnadvanced = 1, kAdvanced = 2, kBound = 4;
// Resource admission for sparse (scope, node) queries; no semantic nesting cap.
constexpr std::size_t kScopeQueriesPerNode = 8;
struct Quota {
  detail::IdfScratch memory;
  std::uint64_t initial, left;
  SinglePassFailure failure{Error::kInput};
  explicit Quota(SemanticBudget b)
      : memory(b.bytes), initial(b.work), left(b.work) {}
  [[noreturn]] void Fail(Error e) {
    failure.kind = e;
    throw failure;
  }
  template <class T>
  void Need(const T& b, Error e = Error::kCorrespondence) {
    if (!bool(b)) Fail(e);
  }
  void Step(std::uint64_t n = 1) {
    if (n > left) Fail(Error::kWorkLimit);
    left -= n;
  }
  SemanticBudget Remaining() const { return {memory.Available(), left}; }
  template <class T>
  void Reserve(std::vector<T>& v, std::size_t n) {
    Need(n <= UINT32_MAX && n <= memory.Available() / sizeof(T),
         Error::kResourceLimit);
    Step(n);
    memory.Retain(n * sizeof(T));
    v.reserve(n);
    memory.Retain((v.capacity() - n) * sizeof(T));
  }
  template <class T>
  void Array(std::vector<T>& v, std::size_t n) {
    Reserve(v, n);
    v.resize(n);
  }
};
SinglePassProtection Context(std::optional<CatchScope> c) {
  return c ? SinglePassProtection{c->region, c->part} : SinglePassProtection{};
}
using Query = std::pair<std::uint32_t, std::uint32_t>;
using CutKey = std::tuple<std::uint32_t, std::uint32_t, std::uint8_t>;
SinglePassScopes Derive(const abc::MethodBody& body, const ValueProgram& g,
                        const PhasedValueInput& in,
                        const ExceptionPlan& regions,
                        const ExceptionControl& legacy,
                        const VerifiedProtocolRegionOwnership& owner,
                        const ForOfScopeWitness& activation, Quota& q) {
  q.Need(!legacy.single_pass, Error::kActivation);
  q.memory.Observe(q.memory.Available());
  auto indexed = IndexForOfIdentities(body, g, in, regions, legacy, owner,
                                      owner.Contract(), q.Remaining());
  const auto* identity = std::get_if<ForOfIdentityIndex>(&indexed);
  if (!identity) {
    const auto e = std::get<ForOfIdentityFailure>(indexed).kind;
    q.Fail(e == ForOfIdentityError::kResourceLimit ? Error::kResourceLimit
           : e == ForOfIdentityError::kWorkLimit   ? Error::kWorkLimit
                                                   : Error::kInput);
  }
  q.Step(identity->work);
  q.memory.Retain(identity->allocated_bytes);
  q.Need(activation.protocol.value < identity->loops.size(),
         Error::kActivation);
  const auto& refused = identity->loops[activation.protocol.value];
  q.Need(!refused.header && activation.acquisition == refused.acquisition &&
             activation.advance == refused.advance,
         Error::kActivation);
  q.Need((activation.kind == ForOfScopeWitnessKind::kMissingNaturalLoop &&
          activation.cause == ForOfIterationError::kAdvance &&
          !activation.borrowed) ||
             (activation.kind == ForOfScopeWitnessKind::kBorrowedHeader &&
              activation.cause == ForOfIterationError::kEntry &&
              activation.borrowed &&
              activation.borrowed->value < legacy.skeleton.loops.size()),
         Error::kActivation);
  // Re-run the actual old counter before trusting its typed activation. A bare
  // Entry/Advance enum or a different failing protocol is never a retry key.
  auto activation_grant = q.Remaining();
  constexpr std::uint64_t kActivationWorkPerInput = 65536;
  const auto dimension =
      std::uint64_t(g.ops.size()) + g.uses.size() + g.edges.size() + 1;
  q.Need(dimension <= UINT32_MAX, Error::kResourceLimit);
  activation_grant.work =
      std::min(activation_grant.work, kActivationWorkPerInput * dimension *
                                          (1 + std::bit_width(dimension)));
  // The failed legacy counter has no work telemetry. Precharge its bounded
  // exclusive grant, including the live successful index retained above.
  q.Step(activation_grant.work);
  auto counted =
      ProveForOfIterations(body, g, in, regions, legacy, owner,
                           owner.Contract(), *identity, activation_grant);
  if (const auto* f = std::get_if<ForOfIterationFailure>(&counted)) {
    if (f->kind == ForOfIterationError::kResourceLimit)
      q.Fail(Error::kResourceLimit);
    if (f->kind == ForOfIterationError::kWorkLimit) q.Fail(Error::kWorkLimit);
    q.Need(f->scope && *f->scope == activation, Error::kActivation);
  } else {
    q.memory.Retain(std::get<ForOfIterationProof>(counted).allocated_bytes);
    q.Need(activation.kind == ForOfScopeWitnessKind::kMissingNaturalLoop,
           Error::kActivation);
  }
  const auto size = in.flow.nodes.size();
  q.Need(size <= UINT32_MAX / kScopeQueriesPerNode, Error::kResourceLimit);
  std::pmr::vector<std::optional<WorkBlockId>> blocks(size, {}, &q.memory),
      commits(g.ops.size(), {}, &q.memory);
  std::pmr::vector<std::optional<IteratorProtocolId>> acquisitions(size, {},
                                                                   &q.memory);
  for (std::uint32_t b = 0; b < g.phases.size(); ++b) {
    q.Step();
    const auto& p = g.phases[b];
    q.Need(p.input.value < size && !blocks[p.input.value]);
    blocks[p.input.value] = WorkBlockId{b};
    if (p.phase == ExceptionPhase::kCommit && p.operation)
      commits[p.operation->value] = WorkBlockId{b};
  }
  std::pmr::vector<std::uint32_t> offsets(&q.memory);
  std::pmr::vector<BlockId> successors(&q.memory);
  offsets.push_back(0);
  for (const auto& node : in.flow.nodes) {
    q.Step();
    for (std::uint8_t e = 0; e < node.successor_count; ++e) {
      q.Step();
      successors.push_back({node.successors[e].target.value});
    }
    offsets.push_back(static_cast<std::uint32_t>(successors.size()));
  }
  auto dominated = BuildDominators({offsets, successors}, {0},
                                   {q.memory.Available(), q.left});
  if (const auto* e = std::get_if<DominatorError>(&dominated))
    q.Fail(*e == DominatorError::kResourceLimit ? Error::kResourceLimit
           : *e == DominatorError::kWorkLimit   ? Error::kWorkLimit
                                                : Error::kInput);
  const auto& dom = std::get<DominatorTree>(dominated);
  q.memory.Retain(dom.work.allocated_bytes);
  q.Step(dom.work.steps + dom.work.eval_steps + dom.work.edge_visits);
  SinglePassScopes out{g.revision, g.cfg_revision, activation};
  q.Array(out.scopes, identity->loops.size());
  q.Array(out.parents, identity->loops.size());
  std::pmr::vector<std::uint32_t> depth(identity->loops.size(), 0, &q.memory);
  for (const auto& l : identity->loops) {
    q.Step();
    const auto node = g.phases[g.ops[l.acquisition.value].block.value].input;
    q.Need(node.value < size && !acquisitions[node.value]);
    acquisitions[node.value] = l.protocol;
  }
  // Every candidate parent is certified original body membership and a real
  // exceptional dominator. Dominators containing one point form a chain.
  for (const auto& r : owner.Summary().regions)
    for (const auto& range : r.boundary->protected_ranges) {
      q.Step();
      if (range.role != ProtectedRangeRole::kLoopBody) continue;
      const auto outer =
          g.phases[g.ops[identity->loops[r.protocol.value].acquisition.value]
                       .block.value]
              .input;
      for (const auto node : range.ordinary) {
        q.Step();
        if (!acquisitions[node.value] ||
            *acquisitions[node.value] == r.protocol)
          continue;
        const auto inner = *acquisitions[node.value];
        q.Need(dom.Dominates({outer.value}, {node.value}), Error::kParent);
        if (dom.nodes[outer.value].begin > depth[inner.value]) {
          depth[inner.value] = dom.nodes[outer.value].begin;
          out.parents[inner.value] = r.protocol;
        }
      }
    }
  std::pmr::vector<std::optional<CloseIterationBoundary>> boundaries(
      identity->loops.size(), {}, &q.memory);
  for (const auto& r : owner.Summary().regions) {
    q.Step();
    auto& b = boundaries[r.protocol.value];
    if (b)
      q.Need(*b == r.iteration);
    else
      b = r.iteration;
  }
  for (const auto& l : identity->loops) {
    q.Step();
    if (l.header) continue;
    q.failure.protocol = l.protocol;
    q.Need(boundaries[l.protocol.value], Error::kEntry);
    const auto& b = *boundaries[l.protocol.value];
    q.Need(!b.loop_header && b.value && commits[l.value.value] &&
               commits[l.decision.value],
           Error::kEntry);
    const auto entry = g.phases[g.ops[l.advance.value].block.value].input;
    const auto value = g.phases[commits[l.value.value]->value].input;
    const auto decision = g.phases[commits[l.decision.value]->value].input;
    q.Need(b.exhausted.source == decision && b.success == l.success &&
               dom.nodes[entry.value].begin,
           Error::kEntry);
    const auto& node = in.flow.nodes[b.exhausted.source.value];
    q.Need(b.exhausted.successor < node.successor_count);
    const auto continuation = node.successors[b.exhausted.successor].target;
    out.scopes[l.protocol.value] =
        SinglePassIteratorScope{l.protocol,
                                l.acquisition,
                                l.advance,
                                l.decision,
                                l.value,
                                entry,
                                decision,
                                value,
                                l.success,
                                continuation,
                                b.exit,
                                b.exhausted,
                                out.parents[l.protocol.value]};
  }
  q.Need(out.scopes[activation.protocol.value], Error::kActivation);
  q.Reserve(out.cuts, owner.Summary().entries.size());
  std::pmr::map<CutKey, std::uint32_t> cuts(&q.memory);
  for (const auto& e : owner.Summary().entries) {
    q.Step();
    if (!out.scopes[e.protocol.value]) continue;
    cuts.emplace(
        CutKey{e.protocol.value, e.source.source.value, e.source.successor},
        static_cast<std::uint32_t>(out.cuts.size()));
    out.cuts.push_back({e.protocol, e.source, e.pending});
  }
  q.Reserve(out.coverage, size * kScopeQueriesPerNode);
  q.Reserve(out.step_exits, g.edges.size());
  q.Reserve(out.dead_entries, g.edges.size());
  for (std::uint32_t n = 0; n < size; ++n) {
    q.Step();
    if (dom.nodes[n].begin) continue;
    const auto& node = in.flow.nodes[n];
    for (std::uint8_t e = 0; e < node.successor_count; ++e) {
      q.Step();
      if (dom.nodes[node.successors[e].target.value].begin) {
        q.Need(out.dead_entries.size() < out.dead_entries.capacity(),
               Error::kResourceLimit);
        out.dead_entries.push_back({{n}, e});
      }
    }
  }
  // Sparse per-activation states. Cut ports are closed by the existing outcome
  // and priority certificates, never guessed from a terminal target or a PC.
  std::pmr::map<Query, std::uint8_t> states(&q.memory);
  std::pmr::deque<Query> pending(&q.memory);
  const auto add = [&](Query key, std::uint8_t bits) {
    q.Step();
    auto at = states.find(key);
    if (at == states.end()) {
      q.Need(states.size() < size * kScopeQueriesPerNode,
             Error::kResourceLimit);
      at = states.emplace(key, 0).first;
      out.coverage.push_back(
          {IteratorProtocolId{key.first}, ExceptionNodeId{key.second}});
    }
    if ((at->second | bits) != at->second) {
      at->second |= bits;
      pending.push_back(key);
    }
  };
  for (const auto& s : out.scopes)
    if (s) add({s->protocol.value, s->entry.value}, kUnadvanced);
  while (!pending.empty()) {
    q.Step();
    const auto key = pending.front();
    pending.pop_front();
    const auto& s = *out.scopes[key.first];
    const auto& l = identity->loops[key.first];
    const ExceptionNodeId n{key.second};
    q.failure.protocol = s.protocol;
    auto bits = states.find(key)->second;
    const auto& node = in.flow.nodes[n.value];
    if (n == s.entry) {
      q.Need(bits == kUnadvanced, Error::kRepeat);
      bits = kAdvanced;
    }
    if (n == s.value_commit) {
      q.Need(bits == kAdvanced, Error::kRepeat);
      bits = kBound;
    }
    if (n == s.decision_commit) q.Need(bits == kAdvanced, Error::kRepeat);
    q.Need(n != s.continuation &&
               (n == s.entry ||
                n != g.phases[g.ops[s.acquisition.value].block.value].input),
           Error::kRepeat);
    if (node.successor_count == 0) {
      q.Need(bits == kBound, Error::kExit);
      const auto& operation =
          in.flow.instructions[node.instruction.value].operation;
      q.Need(std::holds_alternative<ReturnOperation>(operation) ||
                 std::holds_alternative<ThrowOperation>(operation),
             Error::kExit);
    }
    for (std::uint8_t e = 0; e < node.successor_count; ++e) {
      q.Step();
      const CompletionEdge edge{n, e};
      q.failure.edge = edge;
      if (edge == s.exhausted) {
        q.Need(bits == kAdvanced, Error::kExit);
        continue;
      }
      if (cuts.contains({key.first, n.value, e})) {
        q.Need(bits == kBound, Error::kExit);
        continue;
      }
      if (node.successors[e].kind == ExceptionArcKind::kException &&
          bits != kBound) {
        q.Need(blocks[n.value], Error::kEntry);
        const auto op = g.phases[blocks[n.value]->value].operation;
        q.Need(op && (*op == l.advance || *op == l.check || *op == l.done ||
                      *op == l.value),
               Error::kEntry);
        q.Need(out.step_exits.size() < out.step_exits.capacity(),
               Error::kResourceLimit);
        out.step_exits.push_back({s.protocol, edge, node.successors[e].target});
        continue;
      }
      add({key.first, node.successors[e].target.value}, bits);
    }
  }
  std::sort(out.coverage.begin(), out.coverage.end(), [&](auto a, auto b) {
    q.Step();
    return a < b;
  });
  q.Array(out.protection, regions.layout.scopes.size());
  for (std::size_t i = 0; i < out.protection.size(); ++i) {
    q.Step();
    out.protection[i] = Context(regions.layout.scopes[i]);
  }
  q.Array(out.parents_protection, regions.layout.regions.size());
  q.Array(out.dispatch, regions.layout.regions.size());
  for (std::size_t i = 0; i < out.dispatch.size(); ++i) {
    q.Step();
    out.parents_protection[i] = Context(regions.layout.regions[i].parent);
    out.dispatch[i] = regions.layout.regions[i].outer_dispatch;
  }
  out.allocated_bytes =
      out.scopes.capacity() * sizeof(out.scopes[0]) +
      out.parents.capacity() * sizeof(out.parents[0]) +
      out.coverage.capacity() * sizeof(SinglePassMember) +
      out.cuts.capacity() * sizeof(SinglePassCut) +
      out.step_exits.capacity() * sizeof(SinglePassStepExit) +
      out.dead_entries.capacity() * sizeof(CompletionEdge) +
      (out.protection.capacity() + out.parents_protection.capacity()) *
          sizeof(SinglePassProtection) +
      out.dispatch.capacity() * sizeof(out.dispatch[0]);
  out.work = q.initial - q.left;
  return out;
}
}  // namespace
std::variant<SinglePassScopes, SinglePassFailure> RebuildSinglePassScopes(
    const abc::MethodBody& b, const analysis::ValueProgram& g,
    const analysis::PhasedValueInput& i, const ExceptionPlan& r,
    const ExceptionControl& c, const VerifiedProtocolRegionOwnership& o,
    const ForOfScopeWitness& a, analysis::SemanticBudget grant) {
  try {
    Quota q(grant);
    q.Step();
    q.memory.Retain(sizeof(SinglePassScopes));
    if (!c.single_pass) return Derive(b, g, i, r, c, o, a, q);
    auto rebuilt =
        BuildExceptionControl(b, g, i, r, q.Remaining(), &o, c.continuations);
    const auto* legacy = std::get_if<ExceptionControl>(&rebuilt);
    if (!legacy) {
      const auto failure = std::visit(
          [](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, ExceptionControl>)
              return ExceptionControlError::kInput;
            else if constexpr (std::is_same_v<T, ExceptionControlError>)
              return value;
            else
              return value.kind;
          },
          rebuilt);
      q.Fail(failure == ExceptionControlError::kResourceLimit
                 ? Error::kResourceLimit
             : failure == ExceptionControlError::kWorkLimit ? Error::kWorkLimit
                                                            : Error::kInput);
    }
    q.Step(legacy->work);
    q.memory.Retain(legacy->allocated_bytes);
    return Derive(b, g, i, r, *legacy, o, a, q);
  } catch (const SinglePassFailure& f) {
    return f;
  } catch (const std::bad_alloc&) {
    return SinglePassFailure{Error::kResourceLimit};
  } catch (const std::length_error&) {
    return SinglePassFailure{Error::kResourceLimit};
  }
}
std::variant<SinglePassScopes, SinglePassFailure> ProveSinglePassScopes(
    const abc::MethodBody& b, const analysis::ValueProgram& g,
    const analysis::PhasedValueInput& i, const ExceptionPlan& r,
    const ExceptionControl& c, const VerifiedProtocolRegionOwnership& o,
    const ForOfScopeWitness& a, analysis::SemanticBudget grant) {
  if (c.single_pass) return SinglePassFailure{Error::kActivation};
  return RebuildSinglePassScopes(b, g, i, r, c, o, a, grant);
}
std::variant<std::uint64_t, SinglePassFailure> VerifySinglePassScopes(
    const abc::MethodBody& b, const analysis::ValueProgram& g,
    const analysis::PhasedValueInput& i, const ExceptionPlan& r,
    const ExceptionControl& c, const VerifiedProtocolRegionOwnership& o,
    const SinglePassScopes& p, analysis::SemanticBudget grant) {
  try {
    Quota q(grant);
    auto rebuilt =
        RebuildSinglePassScopes(b, g, i, r, c, o, p.activation, grant);
    if (const auto* failure = std::get_if<SinglePassFailure>(&rebuilt))
      return *failure;
    auto e = std::get<SinglePassScopes>(std::move(rebuilt));
    q.Step(e.work);
    q.memory.Retain(e.allocated_bytes);
    q.Step(e.scopes.size() + e.parents.size() + e.coverage.size() +
           e.cuts.size() + e.step_exits.size() + e.dead_entries.size() +
           e.protection.size() + e.parents_protection.size() +
           e.dispatch.size());
    q.Need(p.revision == e.revision && p.cfg_revision == e.cfg_revision);
    q.Need(p.parents == e.parents, Error::kParent);
    q.Need(p.scopes.size() == e.scopes.size(), Error::kEntry);
    for (std::size_t n = 0; n < p.scopes.size(); ++n) {
      q.Step();
      if (p.scopes[n] && e.scopes[n])
        q.Need(p.scopes[n]->enclosing == e.scopes[n]->enclosing,
               Error::kParent);
    }
    q.Need(p.scopes == e.scopes, Error::kEntry);
    q.Need(p.coverage == e.coverage, Error::kCoverage);
    q.Need(p.cuts.size() == e.cuts.size(), Error::kCoverage);
    q.Need(p.cuts == e.cuts, Error::kPayload);
    q.Need(p.dead_entries == e.dead_entries, Error::kRepeat);
    q.Need(p.step_exits == e.step_exits && p.protection == e.protection &&
               p.parents_protection == e.parents_protection &&
               p.dispatch == e.dispatch,
           Error::kProtection);
    return q.initial - q.left;
  } catch (const SinglePassFailure& f) {
    return f;
  } catch (const std::bad_alloc&) {
    return SinglePassFailure{Error::kResourceLimit};
  } catch (const std::length_error&) {
    return SinglePassFailure{Error::kResourceLimit};
  }
}
}  // namespace abcd::source
