#include "iterator_continuation.h"

#include <algorithm>
#include <stdexcept>

#include "../analysis/idf_scratch.h"

namespace abcd::source {
namespace {
using namespace analysis;
using Error = ForOfPlacementError;
struct Quota {
  detail::IdfScratch memory;
  std::uint64_t initial, left;
  ForOfPlacementFailure failure{Error::kInput};
  explicit Quota(SemanticBudget b)
      : memory(b.bytes), initial(b.work), left(b.work) {}
  [[noreturn]] void Fail(Error e) {
    failure.kind = e;
    throw failure;
  }
  void Require(bool b, Error e = Error::kCorrespondence) {
    if (!b) Fail(e);
  }
  void Step(std::uint64_t n = 1) {
    if (n > left) Fail(Error::kWorkLimit);
    left -= n;
  }
  SemanticBudget Remaining() const { return {memory.Available(), left}; }
  template <class T>
  void Reserve(std::vector<T>& v, std::size_t n) {
    Require(n <= UINT32_MAX && n <= memory.Available() / sizeof(T),
            Error::kResourceLimit);
    Step(n);
    memory.Retain(n * sizeof(T));
    v.reserve(n);
    memory.Retain((v.capacity() - n) * sizeof(T));
  }
};
IteratorProtection Context(std::optional<CatchScope> scope) {
  return scope ? IteratorProtection{scope->region, scope->part}
               : IteratorProtection{};
}
struct Task {
  ControlId node;
  std::optional<LoopId> scope;
};
IteratorContinuationProof Derive(
    const ValueProgram& g, const PhasedValueInput& input,
    const ExceptionPlan& regions,
    const VerifiedProtocolRegionOwnership& ownership,
    const ExceptionControl& legacy, const TreeCensusFailure& activation,
    Quota& q) {
  q.failure.activation = activation;
  q.Require(!legacy.continuations &&
                activation.kind == TreeCensusError::kOrphanExpression &&
                activation.operation &&
                activation.operation->value < g.ops.size(),
            Error::kActivation);
  q.Require(legacy.skeleton.revision == g.revision &&
            legacy.skeleton.cfg_revision == g.cfg_revision);
  const auto& summary = ownership.Summary();
  q.Require(summary.revision == g.revision &&
            summary.cfg_revision == g.cfg_revision);
  std::pmr::vector<std::optional<ControlId>> raw(input.flow.nodes.size(), {},
                                                 &q.memory);
  std::pmr::vector<std::optional<WorkBlockId>> blocks(input.flow.nodes.size(),
                                                      {}, &q.memory);
  for (std::uint32_t b = 0; b < g.phases.size(); ++b) {
    q.Step();
    blocks[g.phases[b].input.value] = WorkBlockId{b};
  }
  for (std::uint32_t n = 0; n < legacy.units.size(); ++n) {
    q.Step();
    if (const auto* phase = std::get_if<PhaseControlUnit>(&legacy.units[n])) {
      q.Require(phase->block.value < g.phases.size());
      const auto original = g.phases[phase->block.value].input;
      q.Require(original.value < raw.size() && !raw[original.value]);
      raw[original.value] = ControlId{n};
    }
  }
  // One lexical traversal for the whole method. No graph walk per iterator.
  std::pmr::vector<std::optional<LoopId>> scopes(legacy.skeleton.nodes.size(),
                                                 {}, &q.memory);
  std::pmr::vector<std::uint8_t> seen(scopes.size(), 0, &q.memory);
  std::pmr::vector<Task> tasks(&q.memory);
  tasks.push_back({legacy.skeleton.entry, {}});
  for (std::size_t i = 0; i < tasks.size(); ++i) {
    q.Step();
    const auto [id, scope] = tasks[i];
    q.Require(id.value < scopes.size());
    if (seen[id.value]) {
      q.Require(scopes[id.value] == scope, Error::kCoverage);
      continue;
    }
    seen[id.value] = 1;
    scopes[id.value] = scope;
    const auto& node = legacy.skeleton.nodes[id.value];
    const auto add = [&](std::optional<ControlId> n, std::optional<LoopId> s) {
      if (n) tasks.push_back({*n, s});
    };
    add(node.next, scope);
    if (node.decision) {
      add(node.decision->evaluate, scope);
      add(node.decision->predicate, scope);
    }
    if (node.arms) {
      add(node.arms->taken, scope);
      add(node.arms->fallthrough, scope);
    }
    if(const auto* logical=std::get_if<IteratorScopeControl>(&node.synthetic)) {
      const auto at=std::lower_bound(legacy.iterator_scopes.begin(),legacy.iterator_scopes.end(),logical->protocol,
          [&](const auto& a,auto b){q.Step();return a.protocol<b;});
      q.Require(at!=legacy.iterator_scopes.end() && at->protocol==logical->protocol);
      add(at->body,scope);
    } else if (const auto* loop = std::get_if<LoopControl>(&node.synthetic)) {
      q.Require(loop->loop.value < legacy.skeleton.loops.size());
      add(legacy.skeleton.loops[loop->loop.value].body, loop->loop);
    } else if (id.value < legacy.units.size()) {
      if (const auto* t =
              std::get_if<TryControlUnit>(&legacy.units[id.value])) {
        q.Require(t->region.value < legacy.regions.size() &&
                  legacy.regions[t->region.value]);
        add(legacy.regions[t->region.value]->body, scope);
        add(legacy.regions[t->region.value]->handler, scope);
      }
    }
  }
  std::pmr::vector<std::optional<LoopId>> headers(legacy.units.size(), {},
                                                  &q.memory);
  for (std::uint32_t l = 0; l < legacy.skeleton.loops.size(); ++l) {
    q.Step();
    const auto h = legacy.skeleton.loops[l].header.value;
    q.Require(h < headers.size() && !headers[h]);
    headers[h] = LoopId{l};
  }
  const auto original =
      g.phases[g.ops[activation.operation->value].block.value].input;
  q.Require(original.value < raw.size() && raw[original.value],
            Error::kActivation);
  const auto orphan_scope = scopes[raw[original.value]->value];
  q.Require(orphan_scope.has_value(), Error::kActivation);
  std::pmr::vector<std::optional<CloseIterationBoundary>> boundaries(
      ownership.Scan().candidates.size(), {}, &q.memory);
  for (const auto& region : summary.regions) {
    q.Step();
    q.Require(region.boundary && region.protocol.value < boundaries.size(),
              Error::kInput);
    auto& b = boundaries[region.protocol.value];
    if (b)
      q.Require(*b == region.iteration);
    else
      b = region.iteration;
  }
  IteratorContinuationProof out{g.revision, g.cfg_revision, activation};
  out.single_pass=legacy.single_pass;
  q.Reserve(out.continuations, boundaries.size());
  std::pmr::vector<std::uint8_t> selected(boundaries.size(), 0, &q.memory);
  bool activated = false;
  for (std::uint32_t p = 0; p < boundaries.size(); ++p) {
    q.Step();
    if (!boundaries[p] || !boundaries[p]->loop_header)
      continue;  // No natural-loop exhaustion corridor in a single-pass scope.
    const auto& b = *boundaries[p];
    q.failure.protocol = IteratorProtocolId{p};
    q.failure.edge = b.exhausted;
    q.Require(b.exhausted.source.value < input.flow.nodes.size());
    const auto& source = input.flow.nodes[b.exhausted.source.value];
    q.Require(b.exhausted.successor < source.successor_count);
    const auto target = source.successors[b.exhausted.successor].target;
    q.Require(target.value < raw.size() && b.loop_header->value < raw.size());
    if (!raw[b.loop_header->value] || !raw[target.value]) continue;
    const auto header = *raw[b.loop_header->value];
    if (!headers[header.value]) continue;
    const auto loop = *headers[header.value];
    const auto projected = *raw[target.value];
    const auto& placed = legacy.skeleton.loops[loop.value];
    if (scopes[projected.value] != loop || !placed.exit ||
        placed.exit->value == projected.value)
      continue;
    std::optional<EdgeId> edge;
    q.Require(blocks[b.exhausted.source.value].has_value());
    const auto& source_block =
        g.blocks[blocks[b.exhausted.source.value]->value];
    for (std::uint32_t n = source_block.first_edge.value;
         n < source_block.first_edge.value + source_block.edge_count; ++n) {
      q.Step();
      const auto& e = g.edges[n];
      if (g.phases[e.source.value].input == b.exhausted.source &&
          e.successor == b.exhausted.successor) {
        q.Require(!edge);
        edge = EdgeId{n};
      }
    }
    q.Require(edge.has_value(), Error::kContinuation);
    out.continuations.push_back(
        {IteratorProtocolId{p}, loop, *b.loop_header, b.exhausted, target,
         projected, ControlId{placed.exit->value}, projected, *edge});
    selected[p] = 1;
    activated |= orphan_scope == loop;
  }
  q.Require(activated && !out.continuations.empty(), Error::kActivation);
  std::size_t coverage_count = 0;
  for (const auto& r : summary.regions)
    if (selected[r.protocol.value]) {
      q.Step();
      for (const auto& range : r.boundary->protected_ranges) {
        q.Step();
        if (range.role == ProtectedRangeRole::kLoopBody) {
          q.Require(range.ordinary.size() <= UINT32_MAX - coverage_count,
                    Error::kResourceLimit);
          coverage_count += range.ordinary.size();
        }
      }
    }
  q.Reserve(out.coverage, coverage_count);
  q.Reserve(out.cuts, summary.entries.size());
  for (const auto& r : summary.regions)
    if (selected[r.protocol.value])
      for (const auto& range : r.boundary->protected_ranges)
        if (range.role == ProtectedRangeRole::kLoopBody)
          for (const auto node : range.ordinary) {
            q.Step();
            out.coverage.push_back({r.protocol, node});
          }
  std::sort(out.coverage.begin(), out.coverage.end(),
            [&](const auto& a, const auto& b) {
              q.Step();
              return a < b;
            });
  out.coverage.erase(std::unique(out.coverage.begin(), out.coverage.end()),
                     out.coverage.end());
  for (const auto& e : summary.entries) {
    q.Step();
    if (selected[e.protocol.value])
      out.cuts.push_back({e.protocol, e.source, e.pending});
  }
  q.Reserve(out.contexts, regions.layout.scopes.size());
  for (const auto scope : regions.layout.scopes) {
    q.Step();
    out.contexts.push_back(Context(scope));
  }
  q.Reserve(out.parents, regions.layout.regions.size());
  q.Reserve(out.dispatch, regions.layout.regions.size());
  for (const auto& r : regions.layout.regions) {
    q.Step();
    out.parents.push_back(Context(r.parent));
    out.dispatch.push_back(r.outer_dispatch);
  }
  q.Reserve(out.copies, g.uses.size());
  for (std::uint32_t u = 0; u < g.uses.size(); ++u) {
    q.Step();
    const auto& use = g.uses[u];
    if (use.life != ValueLife::kLive || !use.incoming) continue;
    const auto* edge = std::get_if<EdgeId>(&*use.incoming);
    const auto* target = std::get_if<ValueId>(&use.owner);
    if (!edge || !target) continue;
    q.Require(edge->value < regions.transfers.size());
    const auto& original_edge = g.edges[edge->value];
    out.copies.push_back({UseId{u}, *edge, *target, use.operand,
                          regions.transfers[edge->value], original_edge.source,
                          original_edge.target,
                          g.phases[original_edge.source.value].phase});
  }
  out.allocated_bytes =
      out.continuations.capacity() * sizeof(IteratorContinuation) +
      out.cuts.capacity() * sizeof(IteratorBodyCut) +
      out.coverage.capacity() * sizeof(IteratorContinuationCoverage) +
      (out.contexts.capacity() + out.parents.capacity()) *
          sizeof(IteratorProtection) +
      out.copies.capacity() * sizeof(IteratorCopyPlacement) +
      out.dispatch.capacity() * sizeof(out.dispatch[0]);
  out.work = q.initial - q.left;
  return out;
}
}  // namespace
std::variant<IteratorContinuationProof, ForOfPlacementFailure>
ProveIteratorContinuations(const abc::MethodBody& body,
                           const analysis::ValueProgram& g,
                           const analysis::PhasedValueInput& input,
                           const ExceptionPlan& regions,
                           const VerifiedProtocolRegionOwnership& owner,
                           const ExceptionControl& legacy,
                           const TreeCensusFailure& activation,
                           analysis::SemanticBudget budget) {
  try {
    Quota q(budget);
    q.memory.Retain(sizeof(IteratorContinuationProof));
    q.Step();
    q.failure.activation = activation;
    auto checked = VerifyExceptionControl(body, g, input, regions, legacy,
                                          q.Remaining(), &owner);
    if (const auto* e = std::get_if<ExceptionControlError>(&checked))
      q.Fail(*e == ExceptionControlError::kResourceLimit ? Error::kResourceLimit
             : *e == ExceptionControlError::kWorkLimit   ? Error::kWorkLimit
                                                         : Error::kInput);
    q.Step(std::get<std::uint64_t>(checked));
    return Derive(g, input, regions, owner, legacy, activation, q);
  } catch (const ForOfPlacementFailure& f) {
    return f;
  } catch (const std::bad_alloc&) {
    return ForOfPlacementFailure{Error::kResourceLimit};
  } catch (const std::length_error&) {
    return ForOfPlacementFailure{Error::kResourceLimit};
  }
}
std::variant<std::uint64_t, ForOfPlacementFailure> VerifyIteratorContinuations(
    const abc::MethodBody& body, const analysis::ValueProgram& g,
    const analysis::PhasedValueInput& input, const ExceptionPlan& regions,
    const VerifiedProtocolRegionOwnership& owner,
    const IteratorContinuationProof& proof, analysis::SemanticBudget budget) {
  try {
    Quota q(budget);
    q.memory.Retain(sizeof(IteratorContinuationProof));
    q.Step();
    q.failure.activation = proof.activation;
    auto old =
        BuildExceptionControl(body, g, input, regions, q.Remaining(), &owner,{},proof.single_pass);
    const auto* legacy = std::get_if<ExceptionControl>(&old);
    if (!legacy) {
      const auto error = std::visit(
          [](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, ExceptionControl>)
              return ExceptionControlError::kStructure;
            else if constexpr (std::is_enum_v<T>)
              return value;
            else
              return value.kind;
          },
          old);
      q.Fail(error == ExceptionControlError::kResourceLimit
                 ? Error::kResourceLimit
             : error == ExceptionControlError::kWorkLimit ? Error::kWorkLimit
                                                          : Error::kInput);
    }
    q.Step(legacy->work);
    q.memory.Retain(legacy->allocated_bytes);
    auto expected =
        Derive(g, input, regions, owner, *legacy, proof.activation, q);
    q.Step(expected.continuations.size() + expected.cuts.size() +
           expected.coverage.size() + expected.contexts.size() +
           expected.parents.size() + expected.dispatch.size() +
           expected.copies.size());
    q.Require(proof.single_pass==expected.single_pass);
    q.Require(proof.revision == expected.revision &&
              proof.cfg_revision == expected.cfg_revision);
    q.Require(proof.continuations == expected.continuations,
              Error::kContinuation);
    q.Require(proof.cuts == expected.cuts, Error::kPayload);
    q.Require(proof.coverage == expected.coverage, Error::kCoverage);
    q.Require(proof.contexts == expected.contexts &&
                  proof.parents == expected.parents &&
                  proof.dispatch == expected.dispatch,
              Error::kProtection);
    q.Require(proof.copies == expected.copies, Error::kCopyPlacement);
    return q.initial - q.left;
  } catch (const ForOfPlacementFailure& f) {
    return f;
  } catch (const std::bad_alloc&) {
    return ForOfPlacementFailure{Error::kResourceLimit};
  } catch (const std::length_error&) {
    return ForOfPlacementFailure{Error::kResourceLimit};
  }
}
std::vector<LoopExitConstraint> IteratorContinuationConstraints(
    const analysis::ValueProgram& g, const ExceptionControl& control,
    const IteratorContinuationProof& proof, ControlQuota& q) {
  std::vector<LoopExitConstraint> result;
  q.Size(result, proof.continuations.size());
  std::vector<std::optional<ControlNodeId>> raw;
  q.Size(raw, g.phases.size());
  for (std::uint32_t n = 0; n < control.units.size(); ++n) {
    q.Work();
    if (const auto* phase = std::get_if<PhaseControlUnit>(&control.units[n])) {
      const auto original = g.phases[phase->block.value].input;
      if (original.value >= raw.size()) throw ControlError::kInvalidGraph;
      raw[original.value] = ControlNodeId{n};
    }
  }
  for (std::size_t i = 0; i < result.size(); ++i) {
    q.Work();
    const auto& c = proof.continuations[i];
    if (c.header.value >= raw.size() ||
        c.exhausted.source.value >= raw.size() || !raw[c.header.value] ||
        !raw[c.exhausted.source.value] || c.edge.value >= g.edges.size())
      throw ControlError::kInvalidGraph;
    result[i] = {*raw[c.header.value], *raw[c.exhausted.source.value],
                 g.edges[c.edge.value].kind};
  }
  return result;
}
}  // namespace abcd::source
