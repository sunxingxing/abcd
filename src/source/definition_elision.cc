#include "definition_elision.h"
#include "tree_checks.h"
#include "flow_dominance.h"
#include <stdexcept>
namespace abcd::source {
std::variant<StorageInitializations,TreeStructureError> PlanStorageInitializations(
    const RecoveryFlow& flow,const FlowLiveness& live,const FlowInitializations& initialized,
    std::span<const std::optional<SymbolId>> storage,
    analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);StorageInitializations result;
    budget.Array(result.stores,flow.statements.size());budget.Array(result.blocked,flow.bindings.size());
    for(std::size_t i=0;i<flow.definitions.size();++i) {
      budget.Step();const auto& definition=flow.definitions[i];const auto target=storage[definition.binding.value];
      if(definition.kind!=FlowDefinitionKind::kUndefined || !live.live_definitions[i] || !target)continue;
      if(!definition.declaration || !initialized.Completed(*target,definition.point))result.blocked[definition.binding.value]=1;
      else result.stores[definition.declaration->value]=*target;
    }
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  } catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
std::variant<DefinitionElisions,TreeStructureError> PlanDefinitionElisions(
    const RecoveryFlow& flow,const FlowDemand& demand,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);DefinitionElisions result;
    budget.Array(result.writes,flow.definitions.size());std::size_t count=0;
    for(std::size_t i=0;i<flow.definitions.size();++i) {
      budget.Step();const auto& definition=flow.definitions[i];
      if(demand.definitions[i] || definition.effect!=FlowWriteEffect::kInert)continue;
      switch(definition.kind) {
        case FlowDefinitionKind::kAssignment:case FlowDefinitionKind::kUndefinedStore:
          if(const auto site=definition.WriteSite()) {
            auto evaluation=definition.value;
            const auto region=flow.events[definition.point.value].discarded;
            if(!evaluation&&region)evaluation=flow.discarded[region->value].evaluation;
            result.writes[count++]={*site,evaluation};
          }
          break;
        // Lexical completion is a binding obligation, independently of value
        // demand. Updates retain their conversion/read/successful writeback.
        case FlowDefinitionKind::kRestParameter:case FlowDefinitionKind::kParameter:case FlowDefinitionKind::kUndefined:
        case FlowDefinitionKind::kInitializer:case FlowDefinitionKind::kUpdate:
        case FlowDefinitionKind::kCatch:case FlowDefinitionKind::kFunctionSelf:
        case FlowDefinitionKind::kIterationCopy:break;
      }
    }
    result.writes.resize(count);count=0;
    budget.Array(result.statements,flow.discarded.size());
    for(std::size_t i=0;i<flow.discarded.size();++i) {
      budget.Step();if(!demand.retained[i]&&!flow.discarded[i].initializer)result.statements[count++]=flow.discarded[i].statement;
    }
    result.statements.resize(count);result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  } catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
std::variant<InitialValueElisions,TreeStructureError> FindDeadInitialValues(
    const RecoveryFlow& flow,const FlowDemand& demand,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);InitialValueElisions result;
    budget.Array(result.values,flow.definitions.size());std::size_t count=0;
    for(std::size_t i=0;i<flow.definitions.size();++i) {
      budget.Step();const auto& definition=flow.definitions[i];
      if(definition.kind!=FlowDefinitionKind::kInitializer || demand.definitions[i] ||
          definition.effect!=FlowWriteEffect::kInert || flow.bindings[definition.binding.value].exposure!=FlowBindingExposure::kPrivate || !definition.declaration)continue;
      const auto region=flow.events[definition.point.value].discarded;
      if(!region||flow.discarded[region->value].initializer!=definition.binding)continue;
      result.values[count++]={*definition.declaration,definition.binding,
          demand.retained[region->value]?definition.value:std::nullopt};
    }
    result.values.resize(count);result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  }catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
std::variant<BindingElisions,TreeStructureError> FindUnusedBindings(
    const RecoveryFlow& flow,const FlowDemand& demand,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);BindingElisions result;
    struct Dependency { SymbolId binding; std::optional<std::size_t> next; };
    std::vector<std::optional<std::size_t>> heads;
    std::vector<Dependency> dependencies;
    std::vector<SymbolId> pending;
    std::vector<std::uint8_t> retained;
    budget.Array(heads,flow.bindings.size());budget.Array(retained,flow.bindings.size());
    budget.Array(pending,flow.bindings.size());budget.Array(dependencies,flow.events.size());
    std::size_t queued=0,edges=0;
    const auto retain=[&](SymbolId binding) {
      if(!retained[binding.value]) {retained[binding.value]=1;pending[queued++]=binding;}
    };
    for(std::size_t i=0;i<flow.bindings.size();++i) {
      budget.Step();if(flow.bindings[i].exposure==FlowBindingExposure::kObservable)
        retain(SymbolId{static_cast<std::uint32_t>(i)});
    }
    // Actual surviving syntax needs lexical names even on unreachable paths.
    // Only an inert initializer may lose its reads with its own unused binding.
    // If its binding survives, those reads form binding-retention dependencies,
    // not value-demand roots. No per-binding CFG/subtree walk is necessary.
    for(const auto& event:flow.events) {
      budget.Step();
      if(event.read) {
        const auto owner=event.discarded?flow.discarded[event.discarded->value].initializer:std::nullopt;
        if(owner&&!demand.retained[event.discarded->value]) {
          dependencies[edges]={*event.read,heads[owner->value]};heads[owner->value]=edges++;
        } else retain(*event.read);
      }
      if(event.definition) {
        const auto& definition=flow.definitions[event.definition->value];
        if(definition.kind==FlowDefinitionKind::kUndefined)continue;
        if(definition.kind==FlowDefinitionKind::kInitializer && event.discarded &&
            flow.discarded[event.discarded->value].initializer==definition.binding)continue;
        // Unmodeled initializer evaluation is not evidence of unused storage.
        retain(definition.binding);
      }
    }
    for(std::size_t at=0;at<queued;++at) {
      budget.Step();const auto binding=pending[at];
      for(auto edge=heads[binding.value];edge;edge=dependencies[*edge].next) {
        budget.Step();retain(dependencies[*edge].binding);
      }
    }
    budget.Array(result.declarations,flow.definitions.size());std::size_t count=0;
    for(const auto& definition:flow.definitions) {
      budget.Step();
      if(!definition.declaration || retained[definition.binding.value])continue;
      if(definition.kind==FlowDefinitionKind::kUndefined) {
        const auto region=flow.events[definition.point.value].discarded;
        const auto value=region&&demand.retained[region->value]?flow.discarded[region->value].evaluation:std::nullopt;
        result.declarations[count++]={*definition.declaration,value,{}};
      } else if(definition.kind==FlowDefinitionKind::kInitializer) {
        const auto region=flow.events[definition.point.value].discarded;
        RequireTree(region&&flow.discarded[region->value].initializer==definition.binding,
            TreeStructureError::kOwnership);
        const auto value=demand.retained[region->value]?definition.value:std::nullopt;
        result.declarations[count++]={*definition.declaration,value,definition.write};
      }
    }
    result.declarations.resize(count);result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  } catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
}
