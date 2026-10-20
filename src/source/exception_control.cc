#include "exception_topology.h"

#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using namespace analysis;
using Error = ExceptionControlError;
void Require(bool valid, Error error) { if (!valid) throw error; }
Error Translate(ControlError error) {
  switch (error) {
    case ControlError::kRevision: return Error::kRevision;
    case ControlError::kCycle: return Error::kCycle;
    case ControlError::kCrossEntry: return Error::kCrossEntry;
    case ControlError::kWorkLimit: return Error::kWorkLimit;
    case ControlError::kResourceLimit: return Error::kResourceLimit;
    case ControlError::kInvalidGraph: case ControlError::kUncoveredBlock:
    case ControlError::kIrreducible: case ControlError::kLoopExits: return Error::kStructure;
  }
  return Error::kStructure;
}
void CheckInput(const abc::MethodBody& body, const ValueProgram& graph,
    const PhasedValueInput& input, const ExceptionPlan& regions, ControlQuota& quota) {
  const auto checked = VerifyExceptionPlan(body, graph, input, regions, quota.Remaining());
  if (const auto* error = std::get_if<ExceptionPlanError>(&checked)) {
    switch (*error) {
      case ExceptionPlanError::kResourceLimit: throw Error::kResourceLimit;
      case ExceptionPlanError::kWorkLimit: throw Error::kWorkLimit;
      case ExceptionPlanError::kRevision: throw Error::kRevision;
      case ExceptionPlanError::kInput: case ExceptionPlanError::kLayout:
      case ExceptionPlanError::kBinding: case ExceptionPlanError::kTransfer: throw Error::kInput;
    }
  }
  quota.Work(std::get<std::uint64_t>(checked));
}
template <typename T> void Compare(const std::vector<T>& expected, const std::vector<T>& actual,
                                   ControlQuota& quota) {
  Require(expected.size() == actual.size(), Error::kStructure);
  for (std::size_t i = 0; i < expected.size(); ++i) {
    quota.Work(); Require(expected[i] == actual[i], Error::kStructure);
  }
}
void LoopEntries(ExceptionControl& mapping, const ControlPlan& plan, ControlQuota& quota) {
  // O(V+R+L): a try arm beginning at a loop header enters its wrapper once;
  // the loop body itself still starts at the original projected node.
  std::vector<std::optional<ControlId>> wrappers;
  quota.Size(wrappers, mapping.units.size());
  for (const auto& loop : plan.loops) {
    quota.Work();
    Require(loop.header.value < wrappers.size(), Error::kStructure);
    wrappers[loop.header.value] = loop.wrapper;
  }
  for (auto& region : mapping.regions) {
    quota.Work(); if (!region) continue;
    if (wrappers[region->body.value]) region->body = *wrappers[region->body.value];
    if (wrappers[region->handler.value]) region->handler = *wrappers[region->handler.value];
  }
}
void CheckProjection(const ValueProgram& graph, ExceptionTopology& topology,
                     const ExceptionControl& actual, ControlQuota& quota) {
  const ControlGraphView view{ControlProjection{graph.revision, graph.cfg_revision, topology.blocks, topology.edges}};
  Require(actual.skeleton.loops.size() == topology.loops.loops.size(), Error::kStructure);
  if (actual.skeleton.loops.empty()) VerifyControlSkeleton(view, topology.entries, actual.skeleton, quota);
  else {
    for (std::size_t i = 0; i < topology.loops.loops.size(); ++i) {
      quota.Work();
      Require(actual.skeleton.loops[i].header.value == topology.loops.loops[i].header.value &&
          actual.skeleton.loops[i].parent == topology.loops.loops[i].parent, Error::kStructure);
    }
    VerifyControlRegions(view, topology.scoped_entries, actual.skeleton, quota);
    LoopEntries(topology.mapping, actual.skeleton, quota);
  }
  const auto& mapping = topology.mapping;
  Compare(mapping.units, actual.units, quota);
  Compare(mapping.arcs, actual.arcs, quota);
  Compare(mapping.regions, actual.regions, quota);
  Compare(mapping.inactive_phases, actual.inactive_phases, quota);
  Compare(mapping.region_roles, actual.region_roles, quota);
  Compare(mapping.protocol_ports, actual.protocol_ports, quota);
}
}
std::variant<ExceptionControl, ExceptionControlError, ExceptionControlFailure, ExceptionControlRegionFailure> BuildExceptionControl(
    const abc::MethodBody& body, const ValueProgram& graph,
    const PhasedValueInput& input, const ExceptionPlan& regions, SemanticBudget budget,
    const VerifiedProtocolRegionOwnership* protocols) {
  try {
    ControlQuota quota(budget);
    CheckInput(body, graph, input, regions, quota);
    if(protocols) {
      auto checked=VerifyCloseCompletion(body,protocols->Profile(),graph,input,regions,
          protocols->Keys(),protocols->Scan(),protocols->Summary(),quota.Remaining(),protocols->Contract());
      if(const auto* failure=std::get_if<CloseCompletionFailure>(&checked))
        throw failure->kind==CloseCompletionError::kResourceLimit?Error::kResourceLimit:
            failure->kind==CloseCompletionError::kWorkLimit?Error::kWorkLimit:Error::kStructure;
      quota.Work(std::get<std::uint64_t>(checked));
    }
    auto topology = protocols?ProjectProtocolControl(graph,regions,input,*protocols,quota):
        ProjectExceptionControl(graph, regions, quota);
    const ControlGraphView view{ControlProjection{graph.revision, graph.cfg_revision, topology.blocks, topology.edges}};
    auto skeleton = [&] {
      try { if(protocols && topology.ports.empty()) {
          std::vector<ControlNodeId> roots;quota.Size(roots,topology.entries.size());
          for(std::size_t i=0;i<roots.size();++i)roots[i]={topology.entries[i].value};
          auto discovered=BuildLoopForest(view,roots,quota.Remaining());
          if(const auto* failure=std::get_if<LoopError>(&discovered)) {
            if(*failure==LoopError::kResourceLimit)throw Error::kResourceLimit;
            if(*failure==LoopError::kWorkLimit)throw Error::kWorkLimit;
            throw Error::kStructure;
          }
          auto forest=std::get<LoopForest>(std::move(discovered));
          quota.Work(forest.work);quota.Admit(forest.allocated_bytes,1);
          if(forest.loops.empty())return BuildControlSkeleton(view,topology.entries,quota);
          ControlLoopLayout layout{std::move(forest.loops),std::move(forest.owners)};
          ExtendLoopExitCorridors(view,roots,layout,quota);
          std::vector<ScopedControlEntry> scoped;quota.Size(scoped,roots.size());
          for(std::size_t i=0;i<roots.size();++i)scoped[i]={roots[i],{}};
          return BuildLoopControl(view,scoped,std::move(layout),quota);
        }
        return topology.loops.loops.empty() ? BuildControlSkeleton(view, topology.entries, quota) :
          BuildLoopControl(view, topology.scoped_entries, std::move(topology.loops), quota); }
      catch (ControlFailure failure) {
        const auto locate = [&](ControlLocation& location) {
          if (location.origin != ControlOriginKind::kBlock) return;
          if (location.node >= topology.mapping.units.size()) throw Error::kStructure;
          std::visit([&](const auto& unit) {
            using T = std::decay_t<decltype(unit)>;
            if constexpr (std::is_same_v<T, PhaseControlUnit>) {
              location.origin = ControlOriginKind::kPhase; location.source = unit.block.value;
              const auto& phase = graph.phases[unit.block.value];
              const auto& instruction = input.flow.instructions[phase.instruction.value];
              location.working_pc = instruction.pc;
              location.pc = instruction.origin.value_or(instruction.pc);
            } else if constexpr(std::is_same_v<T, ProtocolCloseUnit>) {
              location.origin=ControlOriginKind::kProtocolClose;location.source=unit.protocol.value;
            } else {
              location.origin = std::is_same_v<T, TryControlUnit> ? ControlOriginKind::kTry : ControlOriginKind::kTryExit;
              location.source = unit.region.value;
            }
          }, topology.mapping.units[location.node]);
        };
        MapControlLocations(failure.control, locate);
        throw ExceptionControlFailure{Translate(failure.kind), failure};
      }
    }();
    if(protocols && topology.ports.empty()) {
      std::vector<ControlNodeId> roots;quota.Size(roots,topology.entries.size());
      for(std::size_t i=0;i<roots.size();++i)roots[i]={topology.entries[i].value};
      VerifyControlRegions(view,roots,skeleton,quota);
    }
    else if (skeleton.loops.empty()) VerifyControlSkeleton(view, topology.entries, skeleton, quota);
    else {
      VerifyControlRegions(view, topology.scoped_entries, skeleton, quota);
      LoopEntries(topology.mapping, skeleton, quota);
    }
    auto result = std::move(topology.mapping);
    result.skeleton = std::move(skeleton);
    result.allocated_bytes = result.skeleton.allocated_bytes + result.units.capacity() * sizeof(ExceptionControlUnit) +
        result.arcs.capacity() * sizeof(ControlArc) + result.regions.capacity() * sizeof(std::optional<TryControlRegion>) +
        result.inactive_phases.capacity() * sizeof(WorkBlockId) +
        result.region_roles.capacity()*sizeof(ExceptionRegionRole) + result.protocol_ports.capacity()*sizeof(CloseOutcomeLeaving);
    result.work = budget.work - quota.RemainingWork();
    return result;
  } catch (ExceptionControlRegionFailure failure) {
    // All locations come from immutable input phases. PC 0 is a real value;
    // synthetic summaries have a region identity, not an invented instruction.
    if(failure.region.value>=body.tries.size())return Error::kInput;
    const auto& region=body.tries[failure.region.value];
    if(region.first_handler>=body.handlers.size())return Error::kInput;
    const auto handler=body.handlers[region.first_handler].offset;
    const auto locate=[&](WorkBlockId block,auto& pc,auto& working) {
      if(block.value>=graph.phases.size())return;
      const auto site=graph.phases[block.value].instruction.value;
      if(site>=input.flow.instructions.size())return;
      const auto& instruction=input.flow.instructions[site];
      pc=instruction.origin.value_or(instruction.pc);working=instruction.pc;
    };
    for(auto& exit:failure.exits) {
      if(exit.from)locate(*exit.from,exit.pc,exit.working_pc);
      locate(exit.target,exit.target_pc,exit.target_working_pc);
      locate(exit.continuation,exit.continuation_pc,exit.continuation_working_pc);
    }
    failure.exception_region=ExceptionRegionEvidence{failure.region,{},{},region.offset,handler,failure.exits};
    if(failure.region.value>=regions.regions.size())return Error::kInput;
    std::optional<abc::CodeOffset> working;
    locate(regions.regions[failure.region.value].body,failure.exception_region->body_pc,working);
    locate(regions.regions[failure.region.value].handler,failure.exception_region->handler_pc,working);
    return failure;
  } catch (const ExceptionControlFailure& failure) { return failure;
  } catch (Error error) { return error;
  } catch (ControlError error) { return Translate(error);
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::variant<std::uint64_t, ExceptionControlError> VerifyExceptionControl(
    const abc::MethodBody& body, const ValueProgram& graph,
    const PhasedValueInput& input, const ExceptionPlan& regions,
    const ExceptionControl& control, SemanticBudget budget,
    const VerifiedProtocolRegionOwnership* protocols) {
  try {
    ControlQuota quota(budget);
    CheckInput(body, graph, input, regions, quota);
    if(protocols) {
      auto checked=VerifyCloseCompletion(body,protocols->Profile(),graph,input,regions,
          protocols->Keys(),protocols->Scan(),protocols->Summary(),quota.Remaining(),protocols->Contract());
      if(const auto* failure=std::get_if<CloseCompletionFailure>(&checked))
        throw failure->kind==CloseCompletionError::kResourceLimit?Error::kResourceLimit:
            failure->kind==CloseCompletionError::kWorkLimit?Error::kWorkLimit:Error::kStructure;
      quota.Work(std::get<std::uint64_t>(checked));
    }
    auto topology = protocols?ProjectProtocolControl(graph,regions,input,*protocols,quota):
        ProjectExceptionControl(graph, regions, quota);
    if(protocols && topology.ports.empty()) {
      const ControlGraphView view{ControlProjection{graph.revision,graph.cfg_revision,topology.blocks,topology.edges}};
      std::vector<ControlNodeId> roots;quota.Size(roots,topology.entries.size());
      for(std::size_t i=0;i<roots.size();++i)roots[i]={topology.entries[i].value};
      VerifyControlRegions(view,roots,control.skeleton,quota);
      Compare(topology.mapping.units,control.units,quota);Compare(topology.mapping.arcs,control.arcs,quota);
      Compare(topology.mapping.regions,control.regions,quota);Compare(topology.mapping.region_roles,control.region_roles,quota);
      Compare(topology.mapping.protocol_ports,control.protocol_ports,quota);
      Compare(topology.mapping.inactive_phases,control.inactive_phases,quota);
    } else CheckProjection(graph, topology, control, quota);
    return budget.work - quota.RemainingWork();
  } catch (const ExceptionControlRegionFailure& failure) { return failure.kind;
  } catch (Error error) { return error;
  } catch (ControlError error) { return Translate(error);
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
