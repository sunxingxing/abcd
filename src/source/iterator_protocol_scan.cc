#include "iterator_protocol_scan.h"

#include "../analysis/idf_scratch.h"
#include "../analysis/value_phi.h"
#include <algorithm>
#include <stdexcept>

namespace abcd::source {
namespace {
using namespace analysis;
struct Quota {
  detail::IdfScratch memory;
  std::uint64_t left, initial;
  explicit Quota(SemanticBudget budget) : memory(budget.bytes), left(budget.work), initial(budget.work) {}
  void Step(std::uint64_t n = 1) {
    if (n > left) throw IteratorScanFailure{IteratorScanError::kWorkLimit};
    left -= n;
  }
  void Require(bool ok, IteratorScanError kind = IteratorScanError::kCorrespondence) {
    if (!ok) throw IteratorScanFailure{kind};
  }
  SemanticBudget Remaining() const { return {memory.Available(), left}; }
  template<class T> void Reserve(std::vector<T>& v, std::size_t count) {
    Require(count <= UINT32_MAX && count <= memory.Available() / sizeof(T), IteratorScanError::kResourceLimit);
    memory.Retain(count * sizeof(T)); v.reserve(count);
    memory.Retain((v.capacity() - count) * sizeof(T));
  }
};
struct Facts {
  ValueIdentities identities;
  std::pmr::vector<std::optional<IteratorProtocolId>> acquired;
  std::pmr::vector<std::optional<IteratorKey>> keys;
  std::pmr::vector<std::optional<OpId>> next;
  std::pmr::vector<std::uint32_t> next_count;
  std::pmr::vector<std::optional<TryId>> enclosing_handler;
  Facts(const ValueProgram& g, const PhasedValueInput& input, const ExceptionPlan& regions,
      const IteratorKeys& names, Quota& q)
      : acquired(g.values.size(), {}, &q.memory), keys(g.ops.size(), {}, &q.memory),
        next(&q.memory), next_count(&q.memory), enclosing_handler(regions.regions.size(), {}, &q.memory) {
    q.Require(g.phases.size() == g.blocks.size() && regions.revision == g.revision &&
        regions.cfg_revision == g.cfg_revision && names.revision == g.revision && names.cfg_revision == g.cfg_revision,
        IteratorScanError::kInput);
    // These borrowed-input analyses enforce their own scratch allowance but
    // their result counters need not include every verifier temporary. Admit
    // the exclusive grant as a conservative peak, never as retained storage.
    q.memory.Observe(q.memory.Available());
    auto imported = VerifyImportedPhases(g, input, q.Remaining());
    if (const auto* failure = std::get_if<ValueImportFailure>(&imported)) {
      q.Require(false, failure->kind == ValueImportError::kResourceLimit ? IteratorScanError::kResourceLimit :
          failure->kind == ValueImportError::kWorkLimit ? IteratorScanError::kWorkLimit : IteratorScanError::kCorrespondence);
    }
    const auto checked = std::get<PhasedImportProof>(imported);
    q.memory.Observe(checked.allocated_bytes); q.Step(checked.work);
    auto canonical = AnalyzeValueIdentities(g, q.Remaining());
    if (const auto* error = std::get_if<ValueSimplifyError>(&canonical)) {
      q.Require(false, *error == ValueSimplifyError::kResourceLimit ? IteratorScanError::kResourceLimit :
          *error == ValueSimplifyError::kWorkLimit ? IteratorScanError::kWorkLimit : IteratorScanError::kIdentity);
    }
    identities = std::get<ValueIdentities>(std::move(canonical));
    q.memory.Retain(identities.allocated_bytes); q.Step(identities.work);
    std::optional<OpId> previous;
    for (const auto& key : names.uses) {
      q.Step(); q.Require(key.operation.value < g.ops.size() && key.origin < names.origins.size() &&
          (!previous || *previous < key.operation), IteratorScanError::kKeys);
      const auto* read = std::get_if<ValueNamedRead>(&g.ops[key.operation.value].operation);
      q.Require(read && read->name == names.origins[key.origin].pool, IteratorScanError::kKeys);
      keys[key.operation.value] = names.origins[key.origin].key; previous = key.operation;
    }
    std::uint32_t count = 0;
    for (std::uint32_t o = 0; o < g.ops.size(); ++o) {
      q.Step(); const auto& op = g.ops[o];
      if (op.life != ValueLife::kLive || !std::holds_alternative<ValueIteratorAcquire>(op.operation)) continue;
      q.Require(op.result.has_value());
      const auto value = identities.canonical[op.result->value];
      q.Require(!acquired[value.value], IteratorScanError::kIdentity);
      acquired[value.value] = IteratorProtocolId{count++};
    }
    next.resize(count); next_count.resize(count);
    for (const auto& key : names.uses) {
      q.Step(); if (keys[key.operation.value] != IteratorKey::kNext) continue;
      if (const auto owner = Base(g, key.operation, q)) {
        ++next_count[owner->value]; next[owner->value] = key.operation;
      }
    }
    // Parent-linked ownership; never rescan enclosing instruction ranges for
    // every acquisition. The layout's source order is already parent-first.
    for (const auto id : regions.layout.source_order) {
      q.Step(); q.Require(id.value < regions.layout.regions.size());
      if (const auto scope = regions.layout.regions[id.value].parent) {
        q.Require(scope->region.value < enclosing_handler.size());
        enclosing_handler[id.value] = scope->part == CatchPart::kHandler ?
            std::optional{scope->region} : enclosing_handler[scope->region.value];
      }
    }
  }
  std::optional<IteratorProtocolId> Base(const ValueProgram& g, OpId operation, Quota& q) const {
    const auto& op = g.ops[operation.value];
    for (std::uint32_t u = 0; u < op.use_count; ++u) {
      q.Step(); const auto& use = g.uses[op.first_use.value + u];
      if (use.role != OperandRole::kBase) continue;
      if (const auto* value = std::get_if<ValueId>(&use.operand))
        return acquired[identities.canonical[value->value].value];
    }
    return {};
  }
};
IteratorOccurrence Locate(const ValueProgram& graph, const PhasedValueInput& input, OpId op, Quota& q) {
  const auto& operation = graph.ops[op.value];
  q.Require(operation.block.value < graph.phases.size());
  const auto& phase = graph.phases[operation.block.value];
  q.Require(phase.phase == ExceptionPhase::kBefore && phase.input.value < input.flow.nodes.size());
  const auto& node = input.flow.nodes[phase.input.value];
  q.Require(node.instruction.value < input.flow.instructions.size());
  const auto& instruction = input.flow.instructions[node.instruction.value];
  return {op, phase.input, instruction.origin, instruction.pc};
}
std::optional<TryId> Dispatch(const PhasedValueInput& input, const IteratorOccurrence& site) {
  const auto& edge = input.flow.nodes[site.before.value].exception;
  return edge && edge->target ? std::optional{edge->target->region} : std::nullopt;
}
}
std::variant<IteratorProtocolScan, IteratorScanFailure> ScanIteratorProtocols(
    const ValueProgram& graph, const PhasedValueInput& input, const ExceptionPlan& regions,
    const IteratorKeys& keys, SemanticBudget budget) {
  try {
    Quota q(budget); Facts facts(graph, input, regions, keys, q);
    IteratorProtocolScan result{keys.method, graph.revision, graph.cfg_revision};
    q.Reserve(result.candidates, facts.next.size());
    q.Require(keys.uses.size() <= UINT32_MAX / 2, IteratorScanError::kResourceLimit);
    q.Reserve(result.regions, keys.uses.size() * 2);
    for (std::uint32_t o = 0; o < graph.ops.size(); ++o) {
      q.Step(); const auto& op = graph.ops[o];
      if (op.life != ValueLife::kLive || !std::holds_alternative<ValueIteratorAcquire>(op.operation)) continue;
      const auto value = facts.identities.canonical[op.result->value];
      const auto id = *facts.acquired[value.value];
      IteratorProtocolCandidate c{id, Locate(graph, input, {o}, q), value};
      c.obligations[static_cast<std::size_t>(IteratorObligation::kIdentity)] = IteratorEvidenceState::kVerified;
      if (facts.next_count[id.value] == 1) c.next = facts.next[id.value];
      // A unique read is a candidate cached definition, NOT an execution-count,
      // closed-use, activation or close-priority proof.
      result.candidates.push_back(c);
    }
    for (const auto& key : keys.uses) {
      q.Step(); if (facts.keys[key.operation.value] != IteratorKey::kReturn) continue;
      const auto owner = facts.Base(graph, key.operation, q); if (!owner) continue;
      const auto site = Locate(graph, input, key.operation, q);
      const auto region = Dispatch(input, site); if (!region) continue;
      q.Require(region->value < regions.layout.regions.size());
      const auto parent = regions.layout.regions[region->value].parent;
      result.regions.push_back({*owner, *region, IteratorRegionRole::kReturnProtection, site,
          parent ? std::optional{parent->region} : std::nullopt});
      if (const auto outer = facts.enclosing_handler[region->value]) {
        const auto above = regions.layout.regions[outer->value].parent;
        result.regions.push_back({*owner, *outer, IteratorRegionRole::kEnclosingHandler, site,
            above ? std::optional{above->region} : std::nullopt});
      }
    }
    result.allocated_bytes = q.memory.Peak(); result.work = q.initial - q.left;
    result.retained_bytes = result.candidates.capacity() * sizeof(IteratorProtocolCandidate) +
        result.regions.capacity() * sizeof(IteratorRegionCandidate);
    return result;
  } catch (IteratorScanFailure failure) { return failure;
  } catch (const std::bad_alloc&) { return IteratorScanFailure{IteratorScanError::kResourceLimit};
  } catch (const std::length_error&) { return IteratorScanFailure{IteratorScanError::kResourceLimit}; }
}
std::variant<std::uint64_t, IteratorScanFailure> VerifyIteratorCandidates(
    const ValueProgram& graph, const PhasedValueInput& input, const ExceptionPlan& regions,
    const IteratorKeys& keys, const IteratorProtocolScan& scan, SemanticBudget budget) {
  try {
    Quota q(budget); Facts facts(graph, input, regions, keys, q);
    q.Require(scan.method == keys.method && scan.revision == graph.revision &&
        scan.cfg_revision == graph.cfg_revision && scan.candidates.size() == facts.next.size());
    std::size_t candidate = 0;
    for (std::uint32_t o = 0; o < graph.ops.size(); ++o) {
      q.Step(); const auto& op = graph.ops[o];
      if (op.life != ValueLife::kLive || !std::holds_alternative<ValueIteratorAcquire>(op.operation)) continue;
      const auto& c = scan.candidates[candidate++];
      const auto value = facts.identities.canonical[op.result->value];
      q.Require(c.id == facts.acquired[value.value] && c.iterator == value && c.acquisition == Locate(graph,input,{o},q));
      q.Require(c.next == (facts.next_count[c.id.value] == 1 ? facts.next[c.id.value] : std::nullopt));
      for (std::size_t i = 0; i < c.obligations.size(); ++i) {
        q.Step(); q.Require(c.obligations[i] == (i == static_cast<std::size_t>(IteratorObligation::kIdentity) ?
            IteratorEvidenceState::kVerified : IteratorEvidenceState::kMissing));
      }
    }
    std::size_t occurrence = 0;
    for (const auto& key : keys.uses) {
      q.Step(); if (facts.keys[key.operation.value] != IteratorKey::kReturn) continue;
      const auto owner = facts.Base(graph, key.operation, q); if (!owner) continue;
      const auto site = Locate(graph, input, key.operation, q); const auto region = Dispatch(input, site);
      if (!region) continue;
      const auto check = [&](TryId id, IteratorRegionRole role) {
        q.Require(occurrence < scan.regions.size()); const auto& actual = scan.regions[occurrence++];
        const auto parent = regions.layout.regions[id.value].parent;
        q.Require(actual.protocol == *owner && actual.region == id && actual.role == role && actual.read == site &&
            actual.parent == (parent ? std::optional{parent->region} : std::nullopt));
      };
      check(*region, IteratorRegionRole::kReturnProtection);
      if (const auto outer = facts.enclosing_handler[region->value]) check(*outer, IteratorRegionRole::kEnclosingHandler);
    }
    q.Require(occurrence == scan.regions.size()); return q.initial - q.left;
  } catch (IteratorScanFailure failure) { return failure;
  } catch (const std::bad_alloc&) { return IteratorScanFailure{IteratorScanError::kResourceLimit};
  } catch (const std::length_error&) { return IteratorScanFailure{IteratorScanError::kResourceLimit}; }
}
}  // namespace abcd::source
