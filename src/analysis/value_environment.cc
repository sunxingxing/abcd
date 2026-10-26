#include "abcd/analysis/value_environment.h"

#include <limits>
#include <new>
#include <stdexcept>

#include "abcd/analysis/environment_renewal.h"
#include "value_edits.h"
#include "value_flow.h"

namespace abcd::analysis {
namespace {
using Error = ValueEnvironmentError;
enum class EnvironmentGraph { kNormal, kPhased };
void Require(bool condition, Error error) { if (!condition) throw error; }
class Quota {
 public:
  explicit Quota(SemanticBudget budget) : initial_(budget), left_(budget) {}
  void Step(std::uint64_t count = 1) { Require(count <= left_.work, Error::kWorkLimit); left_.work -= count; }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= left_.bytes / width, Error::kResourceLimit); left_.bytes -= count * width;
  }
  template <class T> void Size(std::vector<T>& items, std::size_t count) {
    Admit(count, sizeof(T)); Step(count); items.resize(count); Admit(items.capacity() - count, sizeof(T));
  }
  SemanticBudget Remaining() const { return left_; }
  std::uint64_t Work() const { return initial_.work - left_.work; }
 private:
  SemanticBudget initial_, left_;
};
constexpr std::uint64_t kAncestrySteps = std::numeric_limits<std::uint32_t>::digits;
void Flow(const ValueProgram& graph, Quota& quota, EnvironmentGraph kind) {
  if (kind == EnvironmentGraph::kPhased) {
    const auto checked = VerifyPhasedValueFlow(graph, quota.Remaining());
    if (const auto* error = std::get_if<ValueAvailabilityError>(&checked)) {
      if (*error == ValueAvailabilityError::kResourceLimit) throw Error::kResourceLimit;
      if (*error == ValueAvailabilityError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kFlow;
    }
    quota.Step(std::get<PhasedValueFlowProof>(checked).work);
    return;
  }
  const auto checked = VerifyValueFlow(graph, quota.Remaining());
  if (const auto* error = std::get_if<ValueAvailabilityError>(&checked)) {
    if (*error == ValueAvailabilityError::kResourceLimit) throw Error::kResourceLimit;
    if (*error == ValueAvailabilityError::kWorkLimit) throw Error::kWorkLimit;
    throw Error::kFlow;
  }
  quota.Step(std::get<ValueFlowProof>(checked).work);
}
bool SameBinding(const LexicalBinding& a, const LexicalBinding& b) {
  return a.environment == b.environment && a.slot.value == b.slot.value;
}
EnvironmentOperation Operation(const ValueOp& op) {
  if (op.life != ValueLife::kLive) {
    Require(CanErasePureOp(op), Error::kFlow);
    return std::monostate{};
  }
  if (const auto* push = std::get_if<ValueEnvironmentPush>(&op.operation)) return PushEnvironment{push->slots, push->scope};
  if (std::holds_alternative<ValueEnvironmentPop>(op.operation)) return PopEnvironment{};
  if (const auto* load = std::get_if<ValueLexicalLoad>(&op.operation)) return LexicalLoad{load->address};
  if (const auto* store = std::get_if<ValueLexicalStore>(&op.operation))
    return LexicalStore{store->address, Accumulator{}}; // Topology never consumes the scalar RHS.
  return std::monostate{};
}
EnvironmentOperation PhaseOperation(const ValueProgram& graph, const ValuePhaseSite& phase) {
  if (!phase.operation || phase.phase == ExceptionPhase::kCatch) return std::monostate{};
  auto operation = Operation(graph.ops[phase.operation->value]);
  const bool changes_environment = std::holds_alternative<PushEnvironment>(operation) ||
      std::holds_alternative<PopEnvironment>(operation);
  // Allocation/pop change the active chain only on successful Commit. Lexical
  // reads/writes bind at Before; exception edges retain that entry environment.
  if ((phase.phase == ExceptionPhase::kCommit) != changes_environment) return std::monostate{};
  return operation;
}

ValueEnvironments Renewed(const ValueProgram& graph, Quota& quota) {
  Require(graph.environment_renewal && !graph.phases.empty(), Error::kState);
  const auto& owner = *graph.environment_renewal;
  Require(owner.flow && owner.ssa, Error::kState);
  auto built = ProjectRenewedEnvironments(*owner.flow, *owner.ssa, owner.proof,
                                          quota.Remaining());
  const auto* failure = std::get_if<EnvironmentRenewalFailure>(&built);
  if (failure)
    throw failure->kind == EnvironmentRenewalError::kResourceLimit
        ? Error::kResourceLimit
    : failure->kind == EnvironmentRenewalError::kWorkLimit ? Error::kWorkLimit
                                                           : Error::kState;
  auto raw = std::get<EnvironmentFlow>(std::move(built));
  quota.Admit(raw.allocated_bytes, 1);
  quota.Step(raw.instruction_visits);
  ValueEnvironments result{graph.revision, graph.cfg_revision,
                           EnvironmentFlow{std::move(raw.environments)}};
  result.flow.renewal = graph.environment_renewal;
  quota.Size(result.phase_entries, graph.phases.size());
  quota.Size(result.flow.before, graph.ops.size());
  quota.Size(result.normal_after, graph.ops.size());
  quota.Size(result.flow.allocations, raw.allocations.size());
  quota.Admit(raw.bindings.size(), sizeof(BoundLexicalSite));
  result.flow.bindings.reserve(raw.bindings.size());
  std::vector<std::optional<WorkBlockId>> before, commit;
  quota.Size(before, owner.flow->instructions.size());
  quota.Size(commit, before.size());
  for (std::uint32_t b = 0; b < graph.phases.size(); ++b) {
    quota.Step();
    const auto& phase = graph.phases[b];
    Require(phase.input.value < owner.flow->nodes.size(), Error::kOrigin);
    const auto& original = owner.flow->nodes[phase.input.value];
    Require(phase.instruction.value == original.instruction.value &&
                phase.phase == original.phase,
            Error::kOrigin);
    result.phase_entries[b] = raw.before[phase.input.value];
    if (phase.phase == ExceptionPhase::kBefore)
      before[phase.instruction.value] = WorkBlockId{b};
    if (phase.phase == ExceptionPhase::kCommit)
      commit[phase.instruction.value] = WorkBlockId{b};
  }
  for (std::uint32_t i = 0; i < graph.ops.size(); ++i) {
    quota.Step();
    const auto& op = graph.ops[i];
    Require(op.origin.value < graph.origins.size(), Error::kOrigin);
    const auto original = graph.origins[op.origin.value].site.value;
    Require(original < before.size() && before[original], Error::kOrigin);
    result.flow.before[i] = result.phase_entries[before[original]->value];
    if (op.life == ValueLife::kLive &&
        op.exceptional != ExceptionalCompletion::kAlwaysThrow) {
      Require(commit[original].has_value(), Error::kOrigin);
      const auto node = graph.phases[commit[original]->value].input;
      auto state = raw.before[node.value];
      if (const auto* push = std::get_if<ValueEnvironmentPush>(&op.operation)) {
        const auto* input = std::get_if<PushEnvironment>(
            &owner.flow->instructions[original].operation);
        Require(
            input && input->slots == push->slots && input->scope == push->scope,
            Error::kAllocation);
      } else if (std::holds_alternative<ValueEnvironmentPop>(op.operation) &&
                 state) {
        Require(std::holds_alternative<PopEnvironment>(
                    owner.flow->instructions[original].operation),
                Error::kState);
        const auto parent =
            result.flow.environments.Ancestor(*state, LexicalDepth{1});
        Require(std::holds_alternative<EnvironmentRef>(parent), Error::kState);
        state = std::get<EnvironmentRef>(parent);
      }
      result.normal_after[i] = state;
    }
  }
  for (std::size_t i = 0; i < raw.allocations.size(); ++i) {
    quota.Step();
    const auto& a = raw.allocations[i];
    const auto instruction = owner.flow->nodes[a.instruction].instruction.value;
    Require(instruction < graph.input.sites.size() &&
                graph.input.sites[instruction],
            Error::kOrigin);
    const auto op = *graph.input.sites[instruction];
    Require(op.value < graph.ops.size() &&
                graph.ops[op.value].life == ValueLife::kLive &&
                std::holds_alternative<ValueEnvironmentPush>(
                    graph.ops[op.value].operation),
            Error::kAllocation);
    result.flow.allocations[i] = {op.value, a.environment};
    result.normal_after[op.value] = owner.proof.families[a.environment.value];
  }
  for (const auto& b : raw.bindings) {
    quota.Step();
    const auto instruction = owner.flow->nodes[b.instruction].instruction.value;
    Require(instruction < graph.input.sites.size() &&
                graph.input.sites[instruction],
            Error::kBinding);
    const auto op = *graph.input.sites[instruction];
    Require(op.value < graph.ops.size(), Error::kBinding);
    const auto operation = Operation(graph.ops[op.value]);
    const LexicalAddress* address = nullptr;
    if (const auto* load = std::get_if<LexicalLoad>(&operation))
      address = &load->address;
    if (const auto* store = std::get_if<LexicalStore>(&operation))
      address = &store->address;
    Require(address != nullptr, Error::kBinding);
    const auto resolved = result.flow.environments.Resolve(
        *result.flow.before[op.value], *address);
    Require(std::holds_alternative<LexicalBinding>(resolved) &&
                SameBinding(std::get<LexicalBinding>(resolved), b.binding),
            Error::kBinding);
    result.flow.bindings.push_back({op.value, b.binding});
  }
  result.allocated_bytes =
      sizeof(result) + raw.allocated_bytes +
      result.phase_entries.capacity() * sizeof(result.phase_entries[0]) +
      result.flow.before.capacity() * sizeof(result.flow.before[0]) +
      result.normal_after.capacity() * sizeof(result.normal_after[0]);
  result.work = quota.Work();
  return result;
}
std::variant<std::uint64_t, ValueEnvironmentError> VerifyEnvironments(
    const ValueProgram& graph, const ValueEnvironments& environments, SemanticBudget budget,
    EnvironmentGraph kind) {
  try {
    Quota quota(budget);
    Require(environments.revision == graph.revision && environments.cfg_revision == graph.cfg_revision, Error::kRevision);
    Flow(graph, quota, kind);
    if (graph.environment_renewal) {
      Require(kind == EnvironmentGraph::kPhased &&
                  environments.flow.renewal == graph.environment_renewal,
              Error::kState);
      const auto expected = Renewed(graph, quota);
      const auto& a = environments.flow;
      const auto& b = expected.flow;
      Require(a.before == b.before &&
                  environments.phase_entries == expected.phase_entries &&
                  environments.normal_after == expected.normal_after &&
                  a.bindings.size() == b.bindings.size() &&
                  a.allocations.size() == b.allocations.size() &&
                  a.environments.Size() == b.environments.Size(),
              Error::kState);
      for (std::size_t i = 0; i < a.bindings.size(); ++i) {
        quota.Step();
        Require(a.bindings[i].instruction == b.bindings[i].instruction &&
                    SameBinding(a.bindings[i].binding, b.bindings[i].binding),
                Error::kBinding);
      }
      for (std::size_t i = 0; i < a.allocations.size(); ++i) {
        quota.Step();
        Require(
            a.allocations[i].instruction == b.allocations[i].instruction &&
                a.allocations[i].environment == b.allocations[i].environment,
            Error::kAllocation);
      }
      for (std::uint32_t i = 0; i < a.environments.Size(); ++i) {
        quota.Step();
        const auto* x = a.environments.Find({i});
        const auto* y = b.environments.Find({i});
        Require(x && y && x->parent == y->parent &&
                    x->allocation.slots == y->allocation.slots &&
                    x->allocation.scope == y->allocation.scope,
                Error::kAllocation);
      }
      return quota.Work();
    }
    const auto& flow = environments.flow;
    Require(flow.before.size() == graph.ops.size() &&
        environments.normal_after.size() == graph.ops.size(), Error::kState);
    Require(flow.allocations.size() == flow.environments.Size(), Error::kAllocation);
    std::vector<std::optional<EnvironmentId>> allocations;
    std::vector<std::uint8_t> seen;
    quota.Size(allocations, graph.ops.size()); quota.Size(seen, flow.environments.Size());
    for (const auto& allocation : flow.allocations) {
      quota.Step();
      Require(allocation.instruction < allocations.size() && !allocations[allocation.instruction] &&
          allocation.environment.value < seen.size() && !seen[allocation.environment.value], Error::kAllocation);
      Require(std::holds_alternative<PushEnvironment>(Operation(graph.ops[allocation.instruction])), Error::kAllocation);
      allocations[allocation.instruction] = allocation.environment; seen[allocation.environment.value] = 1;
    }
    std::size_t next_binding = 0;
    const auto apply = [&](std::uint32_t id, const EnvironmentOperation& operation, EnvironmentRef& current) {
        const auto* push = std::get_if<PushEnvironment>(&operation);
        if (push) {
          Require(allocations[id].has_value(), Error::kAllocation);
          const auto* definition = flow.environments.Find(*allocations[id]);
          Require(definition && definition->parent == current && definition->allocation.slots == push->slots &&
              definition->allocation.scope == push->scope, Error::kAllocation);
          current = *allocations[id];
        } else if (std::holds_alternative<PopEnvironment>(operation)) {
          quota.Step(kAncestrySteps);
          const auto parent = flow.environments.Ancestor(current, LexicalDepth{1});
          const auto* value = std::get_if<EnvironmentRef>(&parent);
          Require(value != nullptr, Error::kState); current = *value;
        } else {
          const LexicalAddress* address = nullptr;
          if (const auto* load = std::get_if<LexicalLoad>(&operation)) address = &load->address;
          if (const auto* store = std::get_if<LexicalStore>(&operation)) address = &store->address;
          if (!address) return;
          quota.Step(kAncestrySteps);
          const auto binding = flow.environments.Resolve(current, *address);
          const auto* value = std::get_if<LexicalBinding>(&binding);
          Require(value && next_binding < flow.bindings.size() && flow.bindings[next_binding].instruction == id &&
              SameBinding(*value, flow.bindings[next_binding].binding), Error::kBinding);
          ++next_binding;
        }
    };
    const auto& phases = environments.phase_entries;
    if (kind == EnvironmentGraph::kPhased)
      Require(phases.size() == graph.blocks.size() && !phases.empty() &&
          phases[0] == EnvironmentRef{CapturedDepth{0}}, Error::kState);
    else Require(phases.empty() && !flow.before.empty() &&
        flow.before[0] == EnvironmentRef{CapturedDepth{0}}, Error::kState);
    for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
      quota.Step(); const auto& block = graph.blocks[b];
      if (kind == EnvironmentGraph::kNormal) Require(block.op_count != 0, Error::kState);
      const auto& entry = kind == EnvironmentGraph::kPhased ? phases[b] : flow.before[block.first_op.value];
      if (kind == EnvironmentGraph::kPhased) {
        const auto& phase = graph.phases[b];
        if (phase.reachability == PhaseReachability::kUnreachable) {
          // Fresh flow verification admits only provenance in a dead phase.
          Require(!entry, Error::kState);
          continue;
        }
      }
      Require(entry.has_value(), Error::kState);
      auto current = *entry;
      for (std::uint32_t order = 0; order < block.op_count; ++order) {
        quota.Step(); const auto id = block.first_op.value + order;
        Require(flow.before[id] == current, Error::kState);
        if (kind == EnvironmentGraph::kNormal) {
          apply(id, Operation(graph.ops[id]), current);
          const auto expected = graph.ops[id].life != ValueLife::kLive ||
              graph.ops[id].exceptional == ExceptionalCompletion::kAlwaysThrow
              ? std::optional<EnvironmentRef>{} : std::optional{current};
          Require(environments.normal_after[id] == expected, Error::kState);
        }
      }
      if (kind == EnvironmentGraph::kPhased && graph.phases[b].operation)
        apply(graph.phases[b].operation->value, PhaseOperation(graph, graph.phases[b]), current);
      if (kind == EnvironmentGraph::kPhased && graph.phases[b].phase == ExceptionPhase::kCommit &&
          graph.phases[b].operation) {
        const auto id = graph.phases[b].operation->value;
        const auto expected = graph.ops[id].life == ValueLife::kLive
            ? std::optional{current} : std::optional<EnvironmentRef>{};
        Require(environments.normal_after[id] == expected, Error::kState);
      }
      for (std::uint8_t e = 0; e < block.edge_count; ++e) {
        quota.Step();
        const auto target = graph.edges[block.first_edge.value + e].target;
        const auto& incoming = kind == EnvironmentGraph::kPhased ? phases[target.value] :
            flow.before[graph.blocks[target.value].first_op.value];
        Require(incoming == current, Error::kState);
      }
    }
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      quota.Step();
      if (!flow.before[i] || graph.ops[i].life != ValueLife::kLive ||
          graph.ops[i].exceptional == ExceptionalCompletion::kAlwaysThrow)
        Require(!environments.normal_after[i], Error::kState);
    }
    Require(next_binding == flow.bindings.size(), Error::kBinding);
    return quota.Work();
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}

std::variant<ValueEnvironments, ValueEnvironmentError> BuildEnvironments(
    const ValueProgram& graph, SemanticBudget budget, EnvironmentGraph kind) {
  try {
    Quota quota(budget); Flow(graph, quota, kind);
    if (graph.environment_renewal) {
      Require(kind == EnvironmentGraph::kPhased, Error::kState);
      return Renewed(graph, quota);
    }
    quota.Admit(1, sizeof(ValueEnvironments));
    std::vector<EnvironmentSite> sites;
    NormalFlow topology;
    quota.Size(sites, kind == EnvironmentGraph::kPhased ? graph.blocks.size() : graph.ops.size());
    quota.Size(topology.blocks, graph.blocks.size());
    if (kind == EnvironmentGraph::kNormal) for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      quota.Step(); const auto& op = graph.ops[i];
      Require(op.origin.value < graph.origins.size(), Error::kOrigin);
      sites[i] = {graph.origins[op.origin.value].pc, Operation(op)};
    }
    for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
      quota.Step(); const auto& block = graph.blocks[b]; auto& adapted = topology.blocks[b];
      if (kind == EnvironmentGraph::kPhased) {
        const auto& phase = graph.phases[b];
        Require(phase.instruction.value < graph.input.sites.size() && graph.input.sites[phase.instruction.value] &&
            graph.input.sites[phase.instruction.value]->value < graph.ops.size(), Error::kOrigin);
        const auto& op = graph.ops[graph.input.sites[phase.instruction.value]->value];
        Require(op.origin.value < graph.origins.size(), Error::kOrigin);
        sites[b] = {graph.origins[op.origin.value].pc, PhaseOperation(graph, phase)};
        adapted.begin = sites[b].pc; adapted.first_instruction = static_cast<std::uint32_t>(b);
        adapted.instruction_count = 1;
      } else {
        Require(block.op_count != 0, Error::kFlow);
        adapted.begin = sites[block.first_op.value].pc;
        adapted.first_instruction = block.first_op.value; adapted.instruction_count = block.op_count;
      }
      Require(block.edge_count <= adapted.successors.size(), Error::kFlow);
      adapted.successor_count = block.edge_count;
      for (std::uint8_t e = 0; e < block.edge_count; ++e) {
        quota.Step(); const auto& edge = graph.edges[block.first_edge.value + e];
        // The ancestry solver consumes topology only. Keep every phase edge;
        // its source site's Commit/Before effect selects the propagated state.
        const auto edge_kind = kind == EnvironmentGraph::kPhased && edge.kind == FlowEdgeKind::kException
            ? FlowEdgeKind::kTaken : edge.kind;
        adapted.successors[e] = {{edge.target.value}, edge_kind};
      }
    }
    // EnvironmentFlow contract: <=4 visits/site, plus ancestry construction
    // and queries each bounded by the uint32 depth domain. Debit up front.
    constexpr std::uint64_t kTopologyVisits = 4;
    constexpr auto kSiteWork = kTopologyVisits + 2 * kAncestrySteps;
    Require(sites.size() <= quota.Remaining().work / kSiteWork, Error::kWorkLimit);
    quota.Step(sites.size() * kSiteWork);
    auto built = AnalyzeEnvironmentSites(sites, topology, {quota.Remaining().bytes});
    const auto* failure = std::get_if<EnvironmentFlowFailure>(&built);
    if (failure) {
      if (failure->kind == EnvironmentFlowError::kResourceLimit ||
          failure->environment_error == EnvironmentError::kResourceLimit) return Error::kResourceLimit;
      return Error::kState;
    }
    auto flow = std::get<EnvironmentFlow>(std::move(built));
    quota.Admit(flow.allocated_bytes, 1);
    ValueEnvironments result{graph.revision, graph.cfg_revision, std::move(flow)};
    if (kind == EnvironmentGraph::kPhased) {
      result.phase_entries = std::move(result.flow.before);
      quota.Size(result.flow.before, graph.ops.size());
      for (std::size_t i = 0; i < graph.ops.size(); ++i) {
        quota.Step(); result.flow.before[i] = result.phase_entries[graph.ops[i].block.value];
      }
      for (auto& binding : result.flow.bindings) {
        quota.Step(); const auto& phase = graph.phases[binding.instruction];
        Require(phase.phase == ExceptionPhase::kBefore && phase.operation, Error::kBinding);
        binding.instruction = phase.operation->value;
      }
      for (auto& allocation : result.flow.allocations) {
        quota.Step(); const auto& phase = graph.phases[allocation.instruction];
        Require(phase.phase == ExceptionPhase::kCommit && phase.operation, Error::kAllocation);
        allocation.instruction = phase.operation->value;
      }
    }
    // Publish once from the verified ancestry topology. Consumers use this
    // success state instead of interpreting Push/Pop or walking CFG successors.
    quota.Size(result.normal_after, graph.ops.size());
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      quota.Step();
      if (!result.flow.before[i] || graph.ops[i].life != ValueLife::kLive ||
          graph.ops[i].exceptional == ExceptionalCompletion::kAlwaysThrow) continue;
      result.normal_after[i] = result.flow.before[i];
      if (std::holds_alternative<PopEnvironment>(Operation(graph.ops[i]))) {
        quota.Step(kAncestrySteps);
        const auto parent = result.flow.environments.Ancestor(*result.flow.before[i], LexicalDepth{1});
        const auto* state = std::get_if<EnvironmentRef>(&parent);
        Require(state, Error::kState); result.normal_after[i] = *state;
      }
    }
    for (const auto& allocation : result.flow.allocations) {
      quota.Step(); Require(allocation.instruction < result.normal_after.size(), Error::kAllocation);
      result.normal_after[allocation.instruction] = allocation.environment;
    }
    const auto checked = VerifyEnvironments(graph, result, quota.Remaining(), kind);
    if (const auto* error = std::get_if<Error>(&checked)) return *error;
    quota.Step(std::get<std::uint64_t>(checked));
    // Upstream charge includes scratch; retaining it is conservative, not RSS.
    result.allocated_bytes = sizeof(result) + result.flow.allocated_bytes;
    result.allocated_bytes += result.normal_after.capacity() * sizeof(result.normal_after[0]);
    if (kind == EnvironmentGraph::kPhased)
      result.allocated_bytes += result.flow.before.capacity() * sizeof(result.flow.before[0]);
    result.work = quota.Work();
    return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace
std::variant<ValueEnvironments, ValueEnvironmentError> BuildValueEnvironments(
    const ValueProgram& graph, SemanticBudget budget) {
  return BuildEnvironments(graph, budget, EnvironmentGraph::kNormal);
}
std::variant<std::uint64_t, ValueEnvironmentError> VerifyValueEnvironments(
    const ValueProgram& graph, const ValueEnvironments& environments, SemanticBudget budget) {
  return VerifyEnvironments(graph, environments, budget, EnvironmentGraph::kNormal);
}
std::variant<ValueEnvironments, ValueEnvironmentError> BuildPhasedValueEnvironments(
    const ValueProgram& graph, SemanticBudget budget) {
  return BuildEnvironments(graph, budget, EnvironmentGraph::kPhased);
}
std::variant<std::uint64_t, ValueEnvironmentError> VerifyPhasedValueEnvironments(
    const ValueProgram& graph, const ValueEnvironments& environments, SemanticBudget budget) {
  return VerifyEnvironments(graph, environments, budget, EnvironmentGraph::kPhased);
}
}  // namespace abcd::analysis
