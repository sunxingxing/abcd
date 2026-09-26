#include "abcd/analysis/value_environment.h"

#include <limits>
#include <new>
#include <stdexcept>

#include "value_flow.h"
#include "value_edits.h"

namespace abcd::analysis {
namespace {
using Error = ValueEnvironmentError;
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
void Flow(const ValueProgram& graph, Quota& quota) {
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
}  // namespace

std::variant<std::uint64_t, ValueEnvironmentError> VerifyValueEnvironments(
    const ValueProgram& graph, const ValueEnvironments& environments, SemanticBudget budget) {
  try {
    Quota quota(budget);
    Require(environments.revision == graph.revision && environments.cfg_revision == graph.cfg_revision, Error::kRevision);
    Flow(graph, quota);
    const auto& flow = environments.flow;
    Require(flow.before.size() == graph.ops.size(), Error::kState);
    Require(flow.allocations.size() == flow.environments.Size(), Error::kAllocation);
    std::vector<std::optional<EnvironmentId>> allocations;
    std::vector<std::uint8_t> seen;
    quota.Size(allocations, graph.ops.size()); quota.Size(seen, flow.environments.Size());
    for (const auto& allocation : flow.allocations) {
      quota.Step();
      Require(allocation.instruction < allocations.size() && !allocations[allocation.instruction] &&
          allocation.environment.value < seen.size() && !seen[allocation.environment.value], Error::kAllocation);
      allocations[allocation.instruction] = allocation.environment; seen[allocation.environment.value] = 1;
    }
    std::size_t next_binding = 0;
    Require(!flow.before.empty() && flow.before[0] == EnvironmentRef{CapturedDepth{0}}, Error::kState);
    for (const auto& block : graph.blocks) {
      quota.Step();
      Require(block.op_count != 0 && flow.before[block.first_op.value].has_value(), Error::kState);
      auto current = *flow.before[block.first_op.value];
      for (std::uint32_t order = 0; order < block.op_count; ++order) {
        quota.Step();
        const auto id = block.first_op.value + order;
        const auto& op = graph.ops[id];
        Require(flow.before[id] == current, Error::kState);
        const auto operation = Operation(op);
        const auto* push = std::get_if<PushEnvironment>(&operation);
        Require(allocations[id].has_value() == (push != nullptr), Error::kAllocation);
        if (push) {
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
          if (!address) continue;
          quota.Step(kAncestrySteps);
          const auto binding = flow.environments.Resolve(current, *address);
          const auto* value = std::get_if<LexicalBinding>(&binding);
          Require(value && next_binding < flow.bindings.size() && flow.bindings[next_binding].instruction == id &&
              SameBinding(*value, flow.bindings[next_binding].binding), Error::kBinding);
          ++next_binding;
        }
      }
      for (std::uint8_t e = 0; e < block.edge_count; ++e) {
        quota.Step();
        const auto target = graph.edges[block.first_edge.value + e].target;
        Require(flow.before[graph.blocks[target.value].first_op.value] == current, Error::kState);
      }
    }
    Require(next_binding == flow.bindings.size(), Error::kBinding);
    return quota.Work();
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}

std::variant<ValueEnvironments, ValueEnvironmentError> BuildValueEnvironments(
    const ValueProgram& graph, SemanticBudget budget) {
  try {
    Quota quota(budget); Flow(graph, quota);
    quota.Admit(1, sizeof(ValueEnvironments));
    std::vector<EnvironmentSite> sites;
    NormalFlow topology;
    quota.Size(sites, graph.ops.size()); quota.Size(topology.blocks, graph.blocks.size());
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      quota.Step(); const auto& op = graph.ops[i];
      Require(op.origin.value < graph.origins.size(), Error::kOrigin);
      sites[i] = {graph.origins[op.origin.value].pc, Operation(op)};
    }
    for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
      quota.Step(); const auto& block = graph.blocks[b]; auto& adapted = topology.blocks[b];
      Require(block.op_count != 0, Error::kFlow);
      adapted.begin = sites[block.first_op.value].pc;
      adapted.first_instruction = block.first_op.value; adapted.instruction_count = block.op_count;
      adapted.successor_count = block.edge_count;
      for (std::uint8_t e = 0; e < block.edge_count; ++e) {
        quota.Step(); const auto& edge = graph.edges[block.first_edge.value + e];
        adapted.successors[e] = {{edge.target.value}, edge.kind};
      }
    }
    // EnvironmentFlow contract: <=4 visits/site, plus ancestry construction
    // and queries each bounded by the uint32 depth domain. Debit up front.
    constexpr std::uint64_t kTopologyVisits = 4;
    constexpr auto kSiteWork = kTopologyVisits + 2 * kAncestrySteps;
    Require(graph.ops.size() <= quota.Remaining().work / kSiteWork, Error::kWorkLimit);
    quota.Step(graph.ops.size() * kSiteWork);
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
    const auto checked = VerifyValueEnvironments(graph, result, quota.Remaining());
    if (const auto* error = std::get_if<Error>(&checked)) return *error;
    quota.Step(std::get<std::uint64_t>(checked));
    // Upstream charge includes scratch; retaining it is conservative, not RSS.
    result.allocated_bytes = sizeof(result) + result.flow.allocated_bytes;
    result.work = quota.Work();
    return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::analysis
