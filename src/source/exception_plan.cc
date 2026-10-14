#include "exception_plan.h"

#include <new>
#include <algorithm>
#include <stdexcept>

namespace abcd::source {
namespace {
using namespace analysis;
using Error = ExceptionPlanError;
void Require(bool valid, Error error) { if (!valid) throw error; }
class RegionQuota {
 public:
  explicit RegionQuota(SemanticBudget budget) : remaining_(budget) {}
  void Work(std::uint64_t count = 1) {
    Require(count <= remaining_.work, Error::kWorkLimit);
    remaining_.work -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= remaining_.bytes / width, Error::kResourceLimit);
    remaining_.bytes -= count * width;
  }
  template <typename T> void Size(std::vector<T>& values, std::size_t count) {
    Admit(count, sizeof(T)); Work(count); values.resize(count);
    Admit(values.capacity() - count, sizeof(T));
  }
  SemanticBudget Remaining() const { return remaining_; }
 private:
  SemanticBudget remaining_;
};
ExceptionRegions Layout(const abc::MethodBody& body, const ValueProgram& graph,
                        const PhasedValueInput& input, RegionQuota& quota) {
  const auto verified = VerifyImportedPhases(graph, input, quota.Remaining());
  if (const auto* error = std::get_if<ValueImportFailure>(&verified)) {
    switch (error->kind) {
      case ValueImportError::kWorkLimit: throw Error::kWorkLimit;
      case ValueImportError::kResourceLimit: throw Error::kResourceLimit;
      case ValueImportError::kUnsupportedOperation:
      case ValueImportError::kExceptionFlow:
      case ValueImportError::kResumeFlow:
      case ValueImportError::kUnreachableFlow:
      case ValueImportError::kInputMismatch:
      case ValueImportError::kOperandMismatch:
      case ValueImportError::kInvalidPhi:
      case ValueImportError::kInvalidGraph: throw Error::kInput;
    }
  }
  quota.Work(std::get<PhasedImportProof>(verified).work);
  quota.Admit(std::get<PhasedImportProof>(verified).allocated_bytes, 1);
  auto built = BuildExceptionRegions(body, input.flow, quota.Remaining());
  if (const auto* error = std::get_if<CatchRegionFailure>(&built)) {
    if (error->kind == CatchRegionError::kResourceLimit) throw Error::kResourceLimit;
    ExceptionPlanFailure failure{Error::kLayout,*error,{}};
    if (error->region && error->region->value < body.tries.size()) {
      const auto& region=body.tries[error->region->value];
      if(region.first_handler<body.handlers.size()) {
        const auto handler=body.handlers[region.first_handler].offset;
        const auto origin=[&](abc::CodeOffset pc)->std::optional<abc::CodeOffset> {
          const auto it=std::lower_bound(input.flow.instructions.begin(),input.flow.instructions.end(),pc,
              [&](const auto& instruction,auto value){quota.Work();return instruction.pc<value;});
          if(it==input.flow.instructions.end()||it->pc!=pc)return {};
          return it->origin.value_or(it->pc);
        };
        failure.exception_region=ExceptionRegionEvidence{*error->region,origin(region.offset),origin(handler),region.offset,handler};
      }
    }
    throw failure;
  }
  auto result = std::get<ExceptionRegions>(std::move(built));
  quota.Work(result.work_units);
  quota.Admit(result.allocated_bytes, sizeof(std::byte));
  return result;
}
bool SameScope(const std::optional<CatchScope>& a, const std::optional<CatchScope>& b) {
  return a.has_value() == b.has_value() && (!a || (a->region == b->region && a->part == b->part));
}
bool SameRange(InstructionRange a, InstructionRange b) {
  return a.begin == b.begin && a.end == b.end;
}
void CheckLayout(const ExceptionRegions& expected, const ExceptionRegions& actual,
                 RegionQuota& quota) {
  Require(expected.regions.size() == actual.regions.size() &&
      expected.source_order.size() == actual.source_order.size() &&
      expected.scopes.size() == actual.scopes.size(), Error::kLayout);
  for (std::size_t r = 0; r < expected.regions.size(); ++r) {
    quota.Work();
    const auto& a = expected.regions[r];
    const auto& b = actual.regions[r];
    Require(SameRange(a.body, b.body) && SameRange(a.handler, b.handler) &&
        a.handler_id == b.handler_id && SameScope(a.parent, b.parent) &&
        a.outer_dispatch == b.outer_dispatch && a.inactive == b.inactive,
        Error::kLayout);
  }
  for (std::size_t i = 0; i < expected.source_order.size(); ++i) {
    quota.Work(); Require(expected.source_order[i] == actual.source_order[i], Error::kLayout);
  }
  for (std::size_t i = 0; i < expected.scopes.size(); ++i) {
    quota.Work();
    Require(SameScope(expected.scopes[i], actual.scopes[i]), Error::kLayout);
  }
}
struct RegionSite {
  std::optional<WorkBlockId> before;
  std::optional<WorkBlockId> caught;
};
// Index phase/definition identity once. Never search all values per handler or
// derive a catch payload from the accumulator name or from a normal result.
class RegionIndex {
 public:
  RegionIndex(const ValueProgram& graph, const ExceptionRegions& layout, RegionQuota& quota)
      : graph_(graph), layout_(layout) {
    quota.Size(sites_, layout.scopes.size());
    quota.Size(payloads_, graph.blocks.size());
    quota.Size(owners_, graph.blocks.size());
    for (std::size_t b = 0; b < graph.phases.size(); ++b) {
      quota.Work();
      const auto& phase = graph.phases[b];
      Require(phase.instruction.value < sites_.size(), Error::kInput);
      auto& site = sites_[phase.instruction.value];
      const WorkBlockId block{static_cast<std::uint32_t>(b)};
      switch (phase.phase) {
        case ExceptionPhase::kBefore:
          Require(!site.before, Error::kInput); site.before = block; break;
        case ExceptionPhase::kCatch:
          Require(!site.caught, Error::kBinding);
          if (phase.reachability == PhaseReachability::kReachable) site.caught = block;
          break;
        case ExceptionPhase::kCommit: break;
      }
    }
    for (std::size_t v = 0; v < graph.values.size(); ++v) {
      quota.Work();
      if (const auto* caught = std::get_if<CatchDefinition>(&graph.values[v].origin)) {
        Require(caught->block.value < payloads_.size() && !payloads_[caught->block.value], Error::kBinding);
        payloads_[caught->block.value] = ValueId{static_cast<std::uint32_t>(v)};
      }
    }
    for (std::size_t r = 0; r < layout.regions.size(); ++r) {
      quota.Work();
      if (const auto binding = Region(r).binding) {
        Require(!owners_[binding->block.value], Error::kBinding);
        owners_[binding->block.value] = TryId{static_cast<std::uint32_t>(r)};
      }
    }
    for (std::size_t b = 0; b < graph.phases.size(); ++b) {
      quota.Work();
      if (graph.phases[b].phase == ExceptionPhase::kCatch &&
          graph.phases[b].reachability == PhaseReachability::kReachable)
        Require(owners_[b] && payloads_[b], Error::kBinding);
    }
    for (const auto& exit : graph.exceptional_exits) {
      quota.Work();
      if (exit.handler) {
        const auto& handler = *exit.handler;
        Require(handler.entry.value < owners_.size() &&
            owners_[handler.entry.value] == handler.dispatch.region, Error::kTransfer);
      }
    }
  }
  ExceptionRegion Region(std::size_t r) const {
    const auto& region = layout_.regions[r];
    Require(region.body.begin.value < sites_.size() && region.handler.begin.value < sites_.size(),
        Error::kLayout);
    const auto& body = sites_[region.body.begin.value];
    const auto& handler = sites_[region.handler.begin.value];
    Require(body.before && handler.before, Error::kLayout);
    std::optional<CatchBinding> binding;
    if (handler.caught) {
      const auto value = payloads_[handler.caught->value];
      Require(value.has_value(), Error::kBinding);
      binding = CatchBinding{*handler.caught, *value};
    }
    return {*body.before, *handler.before, binding};
  }
  ExceptionTransfer Transfer(const WorkEdge& edge) const {
    if (edge.kind == FlowEdgeKind::kException) {
      const auto owner = owners_[edge.target.value];
      Require(owner.has_value() && graph_.phases[edge.source.value].phase == ExceptionPhase::kBefore,
          Error::kTransfer);
      return RaiseTransfer{*owner};
    }
    if (graph_.phases[edge.source.value].phase == ExceptionPhase::kCatch) {
      const auto owner = owners_[edge.source.value];
      Require(owner && edge.target == Region(owner->value).handler, Error::kTransfer);
      return CatchTransfer{*owner};
    }
    return NormalTransfer{};
  }
 private:
  const ValueProgram& graph_;
  const ExceptionRegions& layout_;
  std::vector<RegionSite> sites_;
  std::vector<std::optional<ValueId>> payloads_;
  std::vector<std::optional<TryId>> owners_;
};
}  // namespace

std::variant<ExceptionPlan, ExceptionPlanError, ExceptionPlanFailure> BuildExceptionPlan(
    const abc::MethodBody& body, const ValueProgram& graph,
    const PhasedValueInput& input, SemanticBudget budget) {
  try {
    RegionQuota quota(budget);
    ExceptionPlan plan;
    plan.revision = graph.revision;
    plan.cfg_revision = graph.cfg_revision;
    plan.layout = Layout(body, graph, input, quota);
    RegionIndex index(graph, plan.layout, quota);
    quota.Size(plan.regions, plan.layout.regions.size());
    quota.Size(plan.transfers, graph.edges.size());
    for (std::size_t r = 0; r < plan.regions.size(); ++r) {
      quota.Work(); plan.regions[r] = index.Region(r);
    }
    for (std::size_t e = 0; e < graph.edges.size(); ++e) {
      quota.Work(); plan.transfers[e] = index.Transfer(graph.edges[e]);
    }
    plan.allocated_bytes = plan.layout.allocated_bytes +
        plan.regions.capacity() * sizeof(ExceptionRegion) +
        plan.transfers.capacity() * sizeof(ExceptionTransfer);
    const auto checked = VerifyExceptionPlan(body, graph, input, plan, quota.Remaining());
    if (const auto* error = std::get_if<Error>(&checked)) return *error;
    quota.Work(std::get<std::uint64_t>(checked));
    plan.work = budget.work - quota.Remaining().work;
    return plan;
  } catch (const ExceptionPlanFailure& failure) { return failure;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::variant<std::uint64_t, ExceptionPlanError> VerifyExceptionPlan(
    const abc::MethodBody& body, const ValueProgram& graph,
    const PhasedValueInput& input, const ExceptionPlan& plan, SemanticBudget budget) {
  try {
    RegionQuota quota(budget);
    Require(plan.revision == graph.revision && plan.cfg_revision == graph.cfg_revision, Error::kRevision);
    const auto layout = Layout(body, graph, input, quota);
    CheckLayout(layout, plan.layout, quota);
    Require(plan.regions.size() == layout.regions.size(), Error::kLayout);
    Require(plan.transfers.size() == graph.edges.size(), Error::kTransfer);
    const RegionIndex index(graph, layout, quota);
    for (std::size_t r = 0; r < plan.regions.size(); ++r) {
      quota.Work(); Require(plan.regions[r] == index.Region(r), Error::kBinding);
    }
    for (std::size_t e = 0; e < graph.edges.size(); ++e) {
      quota.Work(); Require(plan.transfers[e] == index.Transfer(graph.edges[e]), Error::kTransfer);
    }
    return budget.work - quota.Remaining().work;
  } catch (const ExceptionPlanFailure& failure) { return failure.kind;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
